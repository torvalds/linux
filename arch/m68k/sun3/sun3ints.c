 /*
 * linux/arch/m68k/sun3/sun3ints.c -- Sun-3(x) Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/segment.h>
#include <asm/intersil.h>
#include <asm/oplib.h>
#include <asm/sun3ints.h>
#include <linux/seq_file.h>

extern void sun3_leds (unsigned char);
static irqreturn_t sun3_inthandle(int irq, void *dev_id, struct pt_regs *fp);

void sun3_disable_interrupts(void)
{
	sun3_disable_irq(0);
}

void sun3_enable_interrupts(void)
{
	sun3_enable_irq(0);
}

int led_pattern[8] = {
       ~(0x80), ~(0x01),
       ~(0x40), ~(0x02),
       ~(0x20), ~(0x04),
       ~(0x10), ~(0x08)
};

volatile unsigned char* sun3_intreg;

void sun3_insert_irq(irq_node_t **list, irq_node_t *node)
{
}

void sun3_delete_irq(irq_node_t **list, void *dev_id)
{
}

void sun3_enable_irq(unsigned int irq)
{
	*sun3_intreg |=  (1<<irq);
}

void sun3_disable_irq(unsigned int irq)
{
	*sun3_intreg &= ~(1<<irq);
}

inline void sun3_do_irq(int irq, struct pt_regs *fp)
{
	kstat_cpu(0).irqs[SYS_IRQS + irq]++;
	*sun3_intreg &= ~(1<<irq);
	*sun3_intreg |=  (1<<irq);
}

static irqreturn_t sun3_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	sun3_do_irq(irq,fp);
	if(!(kstat_cpu(0).irqs[SYS_IRQS + irq] % 2000))
		sun3_leds(led_pattern[(kstat_cpu(0).irqs[SYS_IRQS+irq]%16000)
			  /2000]);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_int5(int irq, void *dev_id, struct pt_regs *fp)
{
        kstat_cpu(0).irqs[SYS_IRQS + irq]++;
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
        *sun3_intreg &= ~(1<<irq);
        *sun3_intreg |=  (1<<irq);
#ifdef CONFIG_SUN3
	intersil_clear();
#endif
        do_timer(fp);
#ifndef CONFIG_SMP
	update_process_times(user_mode(fp));
#endif
        if(!(kstat_cpu(0).irqs[SYS_IRQS + irq] % 20))
                sun3_leds(led_pattern[(kstat_cpu(0).irqs[SYS_IRQS+irq]%160)
                /20]);
	return IRQ_HANDLED;
}

/* handle requested ints, excepting 5 and 7, which always do the same
   thing */
irqreturn_t (*sun3_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	[0] = sun3_inthandle,
	[1] = sun3_inthandle,
	[2] = sun3_inthandle,
	[3] = sun3_inthandle,
	[4] = sun3_inthandle,
	[5] = sun3_int5,
	[6] = sun3_inthandle,
	[7] = sun3_int7
};

static const char *dev_names[SYS_IRQS] = {
	[5] = "timer",
	[7] = "int7 handler"
};
static void *dev_ids[SYS_IRQS];
static irqreturn_t (*sun3_inthandler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	[5] = sun3_int5,
	[7] = sun3_int7
};
static irqreturn_t (*sun3_vechandler[SUN3_INT_VECS])(int, void *, struct pt_regs *);
static void *vec_ids[SUN3_INT_VECS];
static const char *vec_names[SUN3_INT_VECS];
static int vec_ints[SUN3_INT_VECS];


int show_sun3_interrupts(struct seq_file *p, void *v)
{
	int i;

	for(i = 0; i < (SUN3_INT_VECS-1); i++) {
		if(sun3_vechandler[i] != NULL) {
			seq_printf(p, "vec %3d: %10u %s\n", i+64,
				   vec_ints[i],
				   (vec_names[i]) ? vec_names[i] :
				   "sun3_vechandler");
		}
	}

	return 0;
}

static irqreturn_t sun3_inthandle(int irq, void *dev_id, struct pt_regs *fp)
{
	if(sun3_inthandler[irq] == NULL)
		panic ("bad interrupt %d received (id %p)\n",irq, dev_id);

        kstat_cpu(0).irqs[SYS_IRQS + irq]++;
        *sun3_intreg &= ~(1<<irq);

	sun3_inthandler[irq](irq, dev_ids[irq], fp);
	return IRQ_HANDLED;
}

static irqreturn_t sun3_vec255(int irq, void *dev_id, struct pt_regs *fp)
{
//	intersil_clear();
	return IRQ_HANDLED;
}

void sun3_init_IRQ(void)
{
	int i;

	*sun3_intreg = 1;

	for(i = 0; i < SYS_IRQS; i++)
	{
		if(dev_names[i])
			cpu_request_irq(i, sun3_default_handler[i], 0,
					dev_names[i], NULL);
	}

	for(i = 0; i < 192; i++)
		sun3_vechandler[i] = NULL;

	sun3_vechandler[191] = sun3_vec255;
}

int sun3_request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *, struct pt_regs *),
                      unsigned long flags, const char *devname, void *dev_id)
{

	if(irq < SYS_IRQS) {
		if(sun3_inthandler[irq] != NULL) {
			printk("sun3_request_irq: request for irq %d -- already taken!\n", irq);
			return 1;
		}

		sun3_inthandler[irq] = handler;
		dev_ids[irq] = dev_id;
		dev_names[irq] = devname;

		/* setting devname would be nice */
		cpu_request_irq(irq, sun3_default_handler[irq], 0, devname,
				NULL);

		return 0;
	} else {
		if((irq >= 64) && (irq <= 255)) {
		        int vec;

			vec = irq - 64;
			if(sun3_vechandler[vec] != NULL) {
				printk("sun3_request_irq: request for vec %d -- already taken!\n", irq);
				return 1;
			}

			sun3_vechandler[vec] = handler;
			vec_ids[vec] = dev_id;
			vec_names[vec] = devname;
			vec_ints[vec] = 0;

			return 0;
		}
	}

	printk("sun3_request_irq: invalid irq %d\n", irq);
	return 1;

}

void sun3_free_irq(unsigned int irq, void *dev_id)
{

	if(irq < SYS_IRQS) {
		if(sun3_inthandler[irq] == NULL)
			panic("sun3_free_int: attempt to free unused irq %d\n", irq);
		if(dev_ids[irq] != dev_id)
			panic("sun3_free_int: incorrect dev_id for irq %d\n", irq);

		sun3_inthandler[irq] = NULL;
		return;
	} else if((irq >= 64) && (irq <= 255)) {
		int vec;

		vec = irq - 64;
		if(sun3_vechandler[vec] == NULL)
			panic("sun3_free_int: attempt to free unused vector %d\n", irq);
		if(vec_ids[irq] != dev_id)
			panic("sun3_free_int: incorrect dev_id for vec %d\n", irq);

		sun3_vechandler[vec] = NULL;
		return;
	} else {
		panic("sun3_free_irq: invalid irq %d\n", irq);
	}
}

irqreturn_t sun3_process_int(int irq, struct pt_regs *regs)
{

	if((irq >= 64) && (irq <= 255)) {
		int vec;

		vec = irq - 64;
		if(sun3_vechandler[vec] == NULL)
			panic ("bad interrupt vector %d received\n",irq);

		vec_ints[vec]++;
		return sun3_vechandler[vec](irq, vec_ids[vec], regs);
	} else {
		panic("sun3_process_int: unable to handle interrupt vector %d\n",
		      irq);
	}
}
