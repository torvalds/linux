#if defined(CONFIG_DDR_TEST) && defined(CONFIG_DDR_FREQ)
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

static ssize_t ddr_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
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

    clk_ddr = clk_get(NULL, "ddr");
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
                clk_set_rate(clk_ddr, value * 1000000);
                value = clk_get_rate(clk_ddr) / 1000000;
                printk("success!!! freq=%dMHz\n", value);
                msleep(32);
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
                    clk_set_rate(clk_ddr, value * 1000000);
                    value = clk_get_rate(clk_ddr) / 1000000;
                    printk("success!!! freq=%dMHz\n", value);
                    msleep(32);
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
                    clk_set_rate(clk_ddr, value * 1000000);
                    value = clk_get_rate(clk_ddr) / 1000000;
                    printk("success!!! freq=%dMHz\n", value);
                    msleep(32);
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
#endif // CONFIG_DDR_TEST && CONFIG_DDR_FREQ
