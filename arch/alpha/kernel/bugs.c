
#include <asm/hwrpb.h>
#include <linux/device.h>


#ifdef CONFIG_SYSFS

static int cpu_is_ev6_or_later(void)
{
	struct percpu_struct *cpu;
        unsigned long cputype;

        cpu = (struct percpu_struct *)((char *)hwrpb + hwrpb->processor_offset);
        cputype = cpu->type & 0xffffffff;
        /* Include all of EV6, EV67, EV68, EV7, EV79 and EV69. */
        return (cputype == EV6_CPU) || ((cputype >= EV67_CPU) && (cputype <= EV69_CPU));
}

ssize_t cpu_show_meltdown(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	if (cpu_is_ev6_or_later())
		return sprintf(buf, "Vulnerable\n");
	else
		return sprintf(buf, "Not affected\n");
}

ssize_t cpu_show_spectre_v1(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
	if (cpu_is_ev6_or_later())
		return sprintf(buf, "Vulnerable\n");
	else
		return sprintf(buf, "Not affected\n");
}

ssize_t cpu_show_spectre_v2(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (cpu_is_ev6_or_later())
		return sprintf(buf, "Vulnerable\n");
	else
		return sprintf(buf, "Not affected\n");
}
#endif
