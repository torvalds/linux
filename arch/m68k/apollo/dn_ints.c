#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/timer.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>
#include <asm/apollohw.h>
#include <asm/errno.h>

static irq_handler_t dn_irqs[16];

irqreturn_t dn_process_int(int irq, struct pt_regs *fp)
{
  irqreturn_t res = IRQ_NONE;

  if(dn_irqs[irq-160].handler) {
    res = dn_irqs[irq-160].handler(irq,dn_irqs[irq-160].dev_id,fp);
  } else {
    printk("spurious irq %d occurred\n",irq);
  }

  *(volatile unsigned char *)(pica)=0x20;
  *(volatile unsigned char *)(picb)=0x20;

  return res;
}

void dn_init_IRQ(void) {

  int i;

  for(i=0;i<16;i++) {
    dn_irqs[i].handler=NULL;
    dn_irqs[i].flags=IRQ_FLG_STD;
    dn_irqs[i].dev_id=NULL;
    dn_irqs[i].devname=NULL;
  }

}

int dn_request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname, void *dev_id) {

  if((irq<0) || (irq>15)) {
    printk("Trying to request invalid IRQ\n");
    return -ENXIO;
  }

  if(!dn_irqs[irq].handler) {
    dn_irqs[irq].handler=handler;
    dn_irqs[irq].flags=IRQ_FLG_STD;
    dn_irqs[irq].dev_id=dev_id;
    dn_irqs[irq].devname=devname;
    if(irq<8)
      *(volatile unsigned char *)(pica+1)&=~(1<<irq);
    else
      *(volatile unsigned char *)(picb+1)&=~(1<<(irq-8));

    return 0;
  }
  else {
    printk("Trying to request already assigned irq %d\n",irq);
    return -ENXIO;
  }

}

void dn_free_irq(unsigned int irq, void *dev_id) {

  if((irq<0) || (irq>15)) {
    printk("Trying to free invalid IRQ\n");
    return ;
  }

  if(irq<8)
    *(volatile unsigned char *)(pica+1)|=(1<<irq);
  else
    *(volatile unsigned char *)(picb+1)|=(1<<(irq-8));

  dn_irqs[irq].handler=NULL;
  dn_irqs[irq].flags=IRQ_FLG_STD;
  dn_irqs[irq].dev_id=NULL;
  dn_irqs[irq].devname=NULL;

  return ;

}

void dn_enable_irq(unsigned int irq) {

  printk("dn enable irq\n");

}

void dn_disable_irq(unsigned int irq) {

  printk("dn disable irq\n");

}

int show_dn_interrupts(struct seq_file *p, void *v) {

  printk("dn get irq list\n");

  return 0;

}

struct fb_info *dn_dummy_fb_init(long *mem_start) {

  printk("fb init\n");

  return NULL;

}

void dn_dummy_video_setup(char *options,int *ints) {

  printk("no video yet\n");

}
