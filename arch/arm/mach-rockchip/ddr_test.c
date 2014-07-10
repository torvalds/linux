#if defined(CONFIG_DDR_TEST)
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sched/rt.h>

#include <dt-bindings/clock/ddr.h>

struct ddrtest {
    struct clk *pll;
    struct clk *clk;
    volatile unsigned int freq;
    volatile bool change_end;
    struct task_struct *task;
    wait_queue_head_t wait;
};
static struct ddrtest ddrtest;

static ssize_t ddr_proc_read(struct file *file, char __user *buffer,
			     size_t len, loff_t *data)
{
    char version[]={"V100"};
    u32 i;
    
    printk("ddr_test Version V1.0\n");
    for(i=0;i<len;i++)
    {
        buffer[i] = version[i];
    }
    return 0;
}

static ssize_t ddr_proc_write(struct file *file, const char __user *buffer,
			      size_t len, loff_t *data)
{
    char *cookie_pot;
    char *p;
    uint32_t value, value1, value2;
    uint32_t count, total;
    char tmp;
    struct clk *clk_ddr = NULL;
    int ret = len;
    char *buf = vzalloc(len);

    cookie_pot = buf;

    if (!cookie_pot)
    {
        return -ENOMEM;
    }
    else
    {
        if (copy_from_user( cookie_pot, buffer, len )) {
            ret = -EFAULT;
            goto out;
        }
    }

    clk_ddr = clk_get(NULL, "clk_ddr");
    if (IS_ERR(clk_ddr)) {
        ret = PTR_ERR(clk_ddr);
        clk_ddr = NULL;
        goto out;
    }

    switch(cookie_pot[0])
    {
        case 'c':
        case 'C':
            printk("change ddr freq:\n");
            if(cookie_pot[1] ==':')
            {
                strsep(&cookie_pot,":");
                p=strsep(&cookie_pot,"M");
                value = simple_strtol(p,NULL,10);
                printk("change!!! freq=%dMHz\n", value);
                //clk_set_rate(clk_ddr, value * 1000000);
                ddrtest.freq = value;
                ddrtest.change_end = false;
                wake_up(&ddrtest.wait);
                while(ddrtest.change_end != true);  //wait change freq end
                value = clk_get_rate(clk_ddr) / 1000000;
                printk("success!!! freq=%dMHz\n", value);
                msleep(64);
                printk("\n");
            }
            else
            {
                printk("Error auto change ddr freq debug.\n");
                printk("-->'c&&C' change freq,Example: echo 'c:400M' > ddr_test\n");
            }
            break;

        case 'a':
        case 'A':
            printk("auto change ddr freq test (random):\n");
            if(cookie_pot[1] ==':')
            {
                strsep(&cookie_pot,":");
                p=strsep(&cookie_pot,"M");
                value1 = simple_strtol(p,NULL,10);
                strsep(&cookie_pot,"-");
                p=strsep(&cookie_pot,"M");
                value2 = simple_strtol(p,NULL,10);
                strsep(&cookie_pot,"-");
                p=strsep(&cookie_pot,"T");
                total = simple_strtol(p,NULL,10);

                count = 0;

                while ( count < total )
                {
                    printk("auto change ddr freq test (random):[%d-%d]\n",count,total);
                    do
                    {
                        value = value1 + prandom_u32();
                        value %= value2;
                    }while(value < value1);

                    printk("change!!! freq=%dMHz\n", value);
                    //clk_set_rate(clk_ddr, value * 1000000);
                    ddrtest.freq = value;
                    ddrtest.change_end = false;
                    wake_up(&ddrtest.wait);
                    while(ddrtest.change_end != true);  //wait change freq end
                    value = clk_get_rate(clk_ddr) / 1000000;
                    printk("success!!! freq=%dMHz\n", value);
                    msleep(64);
                    count++;
                }

            }
            else
            {
                printk("Error auto change ddr freq test debug.\n");
                printk("-->'a&&A' auto change ddr freq test (random),Example: echo 'a:200M-400M-1000T' > ddr_test\n");
            }
            break;

        case 'b':
        case 'B':
            printk("auto change ddr freq test (specific):\n");
            if(cookie_pot[1] ==':')
            {
                strsep(&cookie_pot,":");
                p=strsep(&cookie_pot,"M");
                value1 = simple_strtol(p,NULL,10);
                strsep(&cookie_pot,"-");
                p=strsep(&cookie_pot,"M");
                value2 = simple_strtol(p,NULL,10);
                strsep(&cookie_pot,"-");
                p=strsep(&cookie_pot,"T");
                total = simple_strtol(p,NULL,10);

                count = 0;
                tmp = 0;

                while ( count < total )
                {
                    printk("auto change ddr freq test (specific):[%d-%d]\n",count,total);
                    if(tmp == 1)
                    {
                        value = value1;
                        tmp = 0;
                    }
                    else
                    {
                        value = value2;
                        tmp = 1;
                    }

                    printk("change!!! freq=%dMHz\n", value);
                    //clk_set_rate(clk_ddr, value * 1000000);
                    ddrtest.freq = value;
                    ddrtest.change_end = false;
                    wake_up(&ddrtest.wait);
                    while(ddrtest.change_end != true);  //wait change freq end
                    value = clk_get_rate(clk_ddr) / 1000000;
                    printk("success!!! freq=%dMHz\n", value);
                    msleep(64);
                    count++;
                }

            }
            else
            {
                printk("Error auto change ddr freq test debug.\n");
                printk("-->'b&&B' auto change ddr freq test (specific),Example: echo 'a:200M-400M-1000T' > ddr_test\n");
            }
            break;
            
        case 'h':
        case 'H':
        default:
            printk("Help for ddr_ts .\n-->The Cmd list: \n");
            printk("-->'a&&A' auto change ddr freq test (random),Example: echo 'a:200M-400M-100T' > ddr_test\n");
            printk("-->'b&&B' auto change ddr freq test (specific),Example: echo 'b:200M-400M-100T' > ddr_test\n");
            printk("-->'c&&C' change freq,Example: echo 'c:400M' > ddr_test\n");
            break;
    }

out:
    if (clk_ddr)
        clk_put(clk_ddr);
    vfree(buf);
    return ret;
}

static const struct file_operations ddr_proc_fops = {
    .owner		= THIS_MODULE,
};

static void ddrtest_work(unsigned int value)
{

    clk_set_rate(ddrtest.clk, value * 1000000);
    ddrtest.change_end = true;
}

static int ddrtest_task(void *data)
{
    set_freezable();

    do {
        //unsigned long status = ddr.sys_status;
        ddrtest_work(ddrtest.freq);
        wait_event_freezable(ddrtest.wait, (ddrtest.change_end == false ) || kthread_should_stop());
    } while (!kthread_should_stop());

    return 0;
}

static const struct file_operations ddrtest_proc_fops = {
    .owner = THIS_MODULE,
    .read =  ddr_proc_read,
    .write = ddr_proc_write,
};

static int ddr_proc_init(void)
{
    struct proc_dir_entry *ddr_proc_entry;
    int ret=0;
    struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
    init_waitqueue_head(&ddrtest.wait);

    //ddrtest.pll = clk_get(NULL, "ddr_pll");
    ddrtest.clk = clk_get(NULL, "clk_ddr");
    if (IS_ERR(ddrtest.clk)) {
        ret = PTR_ERR(ddrtest.clk);
        ddrtest.clk = NULL;
        pr_err("failed to get ddr clk, error %d\n", ret);
        goto err;
    }
    ddrtest.freq = clk_get_rate(ddrtest.clk)/1000000;

    ddr_proc_entry = proc_create("driver/ddr_ts",
                           S_IRUGO | S_IWUGO | S_IWUSR | S_IRUSR,
                           NULL,&ddrtest_proc_fops);
    
    if(ddr_proc_entry == NULL){
        ret = -ENOMEM;
        pr_err("failed to create proc entry, error %d\n", ret);
        goto err;
    }

    ddrtest.task = kthread_create(ddrtest_task, NULL, "ddrtestd");
    if (IS_ERR(ddrtest.task)) {
        ret = PTR_ERR(ddrtest.task);
        pr_err("failed to create kthread! error %d\n", ret);
        goto err;
    }
    sched_setscheduler_nocheck(ddrtest.task,SCHED_FIFO, &param);
    get_task_struct(ddrtest.task);
    kthread_bind(ddrtest.task, 0);
    wake_up_process(ddrtest.task);

err:
    return 0;
}

late_initcall_sync(ddr_proc_init);
#endif // CONFIG_DDR_TEST 

