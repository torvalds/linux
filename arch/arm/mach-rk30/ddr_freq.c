#include <mach/ddr.h>

#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/clk.h>

#define ddr_print(x...) printk( "DDR DEBUG: " x )

struct ddr {
	int suspend;
	struct early_suspend early_suspend;
	struct clk *ddr_pll;
};

static void ddr_early_suspend(struct early_suspend *h);
static void ddr_late_resume(struct early_suspend *h);

static struct ddr ddr = {
	.early_suspend = {
		.suspend = ddr_early_suspend,
		.resume = ddr_late_resume,
		.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 50,
	},
};

static volatile bool __sramdata cpu1_pause;
static inline bool is_cpu1_paused(void) { smp_rmb(); return cpu1_pause; }
static inline void set_cpu1_pause(bool pause) { cpu1_pause = pause; smp_wmb(); }
#define MAX_TIMEOUT (16000000UL << 6) //>0.64s

static void __ddr_change_freq(void *info)
{
	uint32_t *value = info;
	u32 timeout = MAX_TIMEOUT;

	while (!is_cpu1_paused() && --timeout);
	if (timeout == 0)
		return;

	*value = ddr_change_freq(*value);

	set_cpu1_pause(false);
}

/* Do not use stack, safe on SMP */
static void __sramfunc pause_cpu1(void *info)
{
	u32 timeout = MAX_TIMEOUT;
	unsigned long flags;
	local_irq_save(flags);

	set_cpu1_pause(true);
	while (is_cpu1_paused() && --timeout);

	local_irq_restore(flags);
}

static uint32_t _ddr_change_freq(uint32_t nMHz)
{
	int this_cpu = get_cpu();

	set_cpu1_pause(false);
	if (this_cpu == 0) {
		if (smp_call_function_single(1, (smp_call_func_t)pause_cpu1, NULL, 0) == 0) {
			u32 timeout = MAX_TIMEOUT;
			while (!is_cpu1_paused() && --timeout);
			if (timeout == 0)
				goto out;
		}

		nMHz = ddr_change_freq(nMHz);

		set_cpu1_pause(false);
	} else {
		smp_call_function_single(0, __ddr_change_freq, &nMHz, 0);

		pause_cpu1(NULL);
	}

	clk_set_rate(ddr.ddr_pll, 0);
out:
	put_cpu();

	return nMHz;
}

uint32_t ddr_set_rate(uint32_t nMHz)
{
	_ddr_change_freq(nMHz);
	return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void ddr_early_suspend(struct early_suspend *h)
{
    uint32_t value;

    //Enable auto self refresh  0x01*32 DDR clk cycle
    ddr_set_auto_self_refresh(true);

    value = _ddr_change_freq(120);

    ddr_print("init success!!! freq=%dMHz\n", value);

    return;
}

static void ddr_late_resume(struct early_suspend *h)
{
    uint32_t value;

    //Disable auto self refresh
    ddr_set_auto_self_refresh(false);

    value = _ddr_change_freq(DDR_FREQ);

    ddr_print("init success!!! freq=%dMHz\n", value);

    return;
}

static int rk30_ddr_late_init (void)
{
    ddr.ddr_pll = clk_get(NULL, "ddr_pll");
    register_early_suspend(&ddr.early_suspend);
    return 0;
}
late_initcall(rk30_ddr_late_init);
#endif

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
