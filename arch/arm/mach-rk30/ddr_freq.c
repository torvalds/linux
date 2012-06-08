#include <mach/ddr.h>

#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>

#define ddr_print(x...) printk( "DDR DEBUG: " x )

struct ddr{
    int suspend;
    struct early_suspend	early_suspend;
};
struct ddr *ddr = NULL;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ddr_early_suspend(struct early_suspend *h)
{

    uint32_t value;
    bool cpu1_online;

    //Enable auto self refresh  0x01*32 DDR clk cycle
    ddr_set_auto_self_refresh(0x01);
    
    cpu1_online = cpu_online(1);
    if(cpu1_online)
        cpu_down(1);

    value=ddr_change_freq(100);

    if(cpu1_online)
        cpu_up(1);
    ddr_print("init success!!! freq=%dMHz\n", value);

    return;
}

static void ddr_early_resume(struct early_suspend *h)
{

    uint32_t value;
    bool cpu1_online;

    //Disable auto self refresh
    ddr_set_auto_self_refresh(0x00);

    cpu1_online = cpu_online(1);
    if(cpu1_online)
        cpu_down(1);

    value=ddr_change_freq(DDR_FREQ);

    if(cpu1_online)
        cpu_up(1);
    ddr_print("init success!!! freq=%dMHz\n", value);

    return;
}
#endif


static int rk30_ddr_late_init (void)
{

    ddr = kmalloc(sizeof(struct ddr), GFP_KERNEL);
    if(!ddr)
    {
        ddr_print("%s: kmalloc fail!\n",__FUNCTION__);
        return -ENOMEM;
    }
    memset(ddr, 0, sizeof(struct ddr));
#ifdef CONFIG_HAS_EARLYSUSPEND
    ddr->early_suspend.suspend = ddr_early_suspend;
    ddr->early_suspend.resume = ddr_early_resume;
    ddr->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 50;
    register_early_suspend(&ddr->early_suspend);
#endif	
    return 0;
}
late_initcall(rk30_ddr_late_init);


#ifdef CONFIG_DDR_TEST
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <sound/pcm.h>
#include <linux/random.h>

static ssize_t ddr_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
{
    char *cookie_pot;
    char *p;
    uint32_t value, value1, value2;
    uint32_t count, total;
    char tmp;
    bool cpu1_online;
    cookie_pot = (char *)vmalloc( len );
    memset(cookie_pot,0,len);

    if (!cookie_pot)
    {
        return -ENOMEM;
    }
    else
    {
        if (copy_from_user( cookie_pot, buffer, len ))
            return -EFAULT;
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
                cpu1_online=cpu_online(1);
                if(cpu1_online)
                    cpu_down(1);
                value=ddr_change_freq(value);
                if(cpu1_online)
                    cpu_up(1);
                printk("success!!! freq=%dMHz\n", value);
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
                        value = value1 + random32();
                        value %= value2;
                    }while(value < value1);

                    printk("change!!! freq=%dMHz\n", value);

                    cpu1_online=cpu_online(1);
                    if(cpu1_online)
                        cpu_down(1);
                    msleep(32);
                    value=ddr_change_freq(value);
                    if(cpu1_online)
                        cpu_up(1);
                    printk("success!!! freq=%dMHz\n", value);

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
                    cpu1_online=cpu_online(1);
                    if(cpu1_online)
                        cpu_down(1);
                    msleep(32);
                    value=ddr_change_freq(value);
                    if(cpu1_online)
                        cpu_up(1);
                    printk("success!!! freq=%dMHz\n", value);
                    count++;
                }

            }
            else
            {
                printk("Error auto change ddr freq test debug.\n");
                printk("-->'b&&B' auto change ddr freq test (specific),Example: echo 'a:200M-400M-1000T' > ddr_test\n");
            }
            break;

        default:
            printk("Help for ddr_ts .\n-->The Cmd list: \n");
            printk("-->'a&&A' auto change ddr freq test (random),Example: echo 'a:200M-400M-100T' > ddr_test\n");
            printk("-->'b&&B' auto change ddr freq test (specific),Example: echo 'b:200M-400M-100T' > ddr_test\n");
            printk("-->'c&&C' change freq,Example: echo 'c:400M' > ddr_test\n");
            break;
    }

    return len;
}

static const struct file_operations ddr_proc_fops = {
    .owner		= THIS_MODULE,
};

static int ddr_proc_init(void)
{
    struct proc_dir_entry *ddr_proc_entry;
    ddr_proc_entry = create_proc_entry("driver/ddr_ts", 0777, NULL);
    if(ddr_proc_entry != NULL)
    {
        ddr_proc_entry->write_proc = ddr_proc_write;
        return -1;
    }
    else
    {
        printk("create proc error !\n");
    }
    return 0;
}

late_initcall(ddr_proc_init);
#endif //CONFIG_DDR_TEST
