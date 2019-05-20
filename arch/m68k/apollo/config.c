// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/rtc.h>
#include <linux/vt_kern.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/bootinfo-apollo.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>
#include <asm/apollohw.h>
#include <asm/irq.h>
#include <asm/machdep.h>

u_long sio01_physaddr;
u_long sio23_physaddr;
u_long rtc_physaddr;
u_long pica_physaddr;
u_long picb_physaddr;
u_long cpuctrl_physaddr;
u_long timer_physaddr;
u_long apollo_model;

extern void dn_sched_init(irq_handler_t handler);
extern void dn_init_IRQ(void);
extern int dn_dummy_hwclk(int, struct rtc_time *);
extern void dn_dummy_reset(void);
#ifdef CONFIG_HEARTBEAT
static void dn_heartbeat(int on);
#endif
static irqreturn_t dn_timer_int(int irq,void *);
static void dn_get_model(char *model);
static const char *apollo_models[] = {
	[APOLLO_DN3000-APOLLO_DN3000] = "DN3000 (Otter)",
	[APOLLO_DN3010-APOLLO_DN3000] = "DN3010 (Otter)",
	[APOLLO_DN3500-APOLLO_DN3000] = "DN3500 (Cougar II)",
	[APOLLO_DN4000-APOLLO_DN3000] = "DN4000 (Mink)",
	[APOLLO_DN4500-APOLLO_DN3000] = "DN4500 (Roadrunner)"
};

int __init apollo_parse_bootinfo(const struct bi_record *record)
{
	int unknown = 0;
	const void *data = record->data;

	switch (be16_to_cpu(record->tag)) {
	case BI_APOLLO_MODEL:
		apollo_model = be32_to_cpup(data);
		break;

	default:
		 unknown=1;
	}

	return unknown;
}

static void __init dn_setup_model(void)
{
	pr_info("Apollo hardware found: [%s]\n",
		apollo_models[apollo_model - APOLLO_DN3000]);

	switch(apollo_model) {
		case APOLLO_UNKNOWN:
			panic("Unknown apollo model");
			break;
		case APOLLO_DN3000:
		case APOLLO_DN3010:
			sio01_physaddr=SAU8_SIO01_PHYSADDR;
			rtc_physaddr=SAU8_RTC_PHYSADDR;
			pica_physaddr=SAU8_PICA;
			picb_physaddr=SAU8_PICB;
			cpuctrl_physaddr=SAU8_CPUCTRL;
			timer_physaddr=SAU8_TIMER;
			break;
		case APOLLO_DN4000:
			sio01_physaddr=SAU7_SIO01_PHYSADDR;
			sio23_physaddr=SAU7_SIO23_PHYSADDR;
			rtc_physaddr=SAU7_RTC_PHYSADDR;
			pica_physaddr=SAU7_PICA;
			picb_physaddr=SAU7_PICB;
			cpuctrl_physaddr=SAU7_CPUCTRL;
			timer_physaddr=SAU7_TIMER;
			break;
		case APOLLO_DN4500:
			panic("Apollo model not yet supported");
			break;
		case APOLLO_DN3500:
			sio01_physaddr=SAU7_SIO01_PHYSADDR;
			sio23_physaddr=SAU7_SIO23_PHYSADDR;
			rtc_physaddr=SAU7_RTC_PHYSADDR;
			pica_physaddr=SAU7_PICA;
			picb_physaddr=SAU7_PICB;
			cpuctrl_physaddr=SAU7_CPUCTRL;
			timer_physaddr=SAU7_TIMER;
			break;
		default:
			panic("Undefined apollo model");
			break;
	}


}

int dn_serial_console_wait_key(struct console *co) {

	while(!(sio01.srb_csrb & 1))
		barrier();
	return sio01.rhrb_thrb;
}

void dn_serial_console_write (struct console *co, const char *str,unsigned int count)
{
   while(count--) {
	if (*str == '\n') {
	sio01.rhrb_thrb = (unsigned char)'\r';
	while (!(sio01.srb_csrb & 0x4))
                ;
	}
    sio01.rhrb_thrb = (unsigned char)*str++;
    while (!(sio01.srb_csrb & 0x4))
            ;
  }
}

void dn_serial_print (const char *str)
{
    while (*str) {
        if (*str == '\n') {
            sio01.rhrb_thrb = (unsigned char)'\r';
            while (!(sio01.srb_csrb & 0x4))
                ;
        }
        sio01.rhrb_thrb = (unsigned char)*str++;
        while (!(sio01.srb_csrb & 0x4))
            ;
    }
}

void __init config_apollo(void)
{
	int i;

	dn_setup_model();

	mach_sched_init=dn_sched_init; /* */
	mach_init_IRQ=dn_init_IRQ;
	mach_max_dma_address = 0xffffffff;
	mach_hwclk           = dn_dummy_hwclk; /* */
	mach_reset	     = dn_dummy_reset;  /* */
#ifdef CONFIG_HEARTBEAT
	mach_heartbeat = dn_heartbeat;
#endif
	mach_get_model       = dn_get_model;

	cpuctrl=0xaa00;

	/* clear DMA translation table */
	for(i=0;i<0x400;i++)
		addr_xlat_map[i]=0;

}

irqreturn_t dn_timer_int(int irq, void *dev_id)
{
	irq_handler_t timer_handler = dev_id;

	volatile unsigned char x;

	timer_handler(irq, dev_id);

	x = *(volatile unsigned char *)(apollo_timer + 3);
	x = *(volatile unsigned char *)(apollo_timer + 5);

	return IRQ_HANDLED;
}

void dn_sched_init(irq_handler_t timer_routine)
{
	/* program timer 1 */
	*(volatile unsigned char *)(apollo_timer + 3) = 0x01;
	*(volatile unsigned char *)(apollo_timer + 1) = 0x40;
	*(volatile unsigned char *)(apollo_timer + 5) = 0x09;
	*(volatile unsigned char *)(apollo_timer + 7) = 0xc4;

	/* enable IRQ of PIC B */
	*(volatile unsigned char *)(pica+1)&=(~8);

#if 0
	pr_info("*(0x10803) %02x\n",
		*(volatile unsigned char *)(apollo_timer + 0x3));
	pr_info("*(0x10803) %02x\n",
		*(volatile unsigned char *)(apollo_timer + 0x3));
#endif

	if (request_irq(IRQ_APOLLO, dn_timer_int, 0, "time", timer_routine))
		pr_err("Couldn't register timer interrupt\n");
}

int dn_dummy_hwclk(int op, struct rtc_time *t) {


  if(!op) { /* read */
    t->tm_sec=rtc->second;
    t->tm_min=rtc->minute;
    t->tm_hour=rtc->hours;
    t->tm_mday=rtc->day_of_month;
    t->tm_wday=rtc->day_of_week;
    t->tm_mon = rtc->month - 1;
    t->tm_year=rtc->year;
    if (t->tm_year < 70)
	t->tm_year += 100;
  } else {
    rtc->second=t->tm_sec;
    rtc->minute=t->tm_min;
    rtc->hours=t->tm_hour;
    rtc->day_of_month=t->tm_mday;
    if(t->tm_wday!=-1)
      rtc->day_of_week=t->tm_wday;
    rtc->month = t->tm_mon + 1;
    rtc->year = t->tm_year % 100;
  }

  return 0;

}

void dn_dummy_reset(void) {

  dn_serial_print("The end !\n");

  for(;;);

}

void dn_dummy_waitbut(void) {

  dn_serial_print("waitbut\n");

}

static void dn_get_model(char *model)
{
    strcpy(model, "Apollo ");
    if (apollo_model >= APOLLO_DN3000 && apollo_model <= APOLLO_DN4500)
        strcat(model, apollo_models[apollo_model - APOLLO_DN3000]);
}

#ifdef CONFIG_HEARTBEAT
static int dn_cpuctrl=0xff00;

static void dn_heartbeat(int on) {

	if(on) {
		dn_cpuctrl&=~0x100;
		cpuctrl=dn_cpuctrl;
	}
	else {
		dn_cpuctrl&=~0x100;
		dn_cpuctrl|=0x100;
		cpuctrl=dn_cpuctrl;
	}
}
#endif

