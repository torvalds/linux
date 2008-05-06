/*
 * drivers/pcmcia/sa1100_jornada720.c
 *
 * Jornada720 PCMCIA specific routines
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/mach-types.h>

#include "sa1111_generic.h"

#define SOCKET0_POWER   GPIO_GPIO0
#define SOCKET0_3V      GPIO_GPIO2
#define SOCKET1_POWER   (GPIO_GPIO1 | GPIO_GPIO3)
#warning *** Does SOCKET1_3V actually do anything?
#define SOCKET1_3V	GPIO_GPIO3

static int jornada720_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
  /*
   * What is all this crap for?
   */
  GRER |= 0x00000002;
  /* Set GPIO_A<3:1> to be outputs for PCMCIA/CF power controller: */
  sa1111_set_io_dir(SA1111_DEV(skt->dev), GPIO_A0|GPIO_A1|GPIO_A2|GPIO_A3, 0, 0);
  sa1111_set_io(SA1111_DEV(skt->dev), GPIO_A0|GPIO_A1|GPIO_A2|GPIO_A3, 0);
  sa1111_set_sleep_io(SA1111_DEV(skt->dev), GPIO_A0|GPIO_A1|GPIO_A2|GPIO_A3, 0);

  return sa1111_pcmcia_hw_init(skt);
}

static int
jornada720_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
  unsigned int pa_dwr_mask, pa_dwr_set;
  int ret;

printk("%s(): config socket %d vcc %d vpp %d\n", __func__,
	skt->nr, state->Vcc, state->Vpp);

  switch (skt->nr) {
  case 0:
    pa_dwr_mask = SOCKET0_POWER | SOCKET0_3V;

    switch (state->Vcc) {
    default:
    case 0:	pa_dwr_set = 0;					break;
    case 33:	pa_dwr_set = SOCKET0_POWER | SOCKET0_3V;	break;
    case 50:	pa_dwr_set = SOCKET0_POWER;			break;
    }
    break;

  case 1:
    pa_dwr_mask = SOCKET1_POWER;

    switch (state->Vcc) {
    default:
    case 0:	pa_dwr_set = 0;					break;
    case 33:	pa_dwr_set = SOCKET1_POWER;			break;
    case 50:	pa_dwr_set = SOCKET1_POWER;			break;
    }
    break;

  default:
    return -1;
  }

  if (state->Vpp != state->Vcc && state->Vpp != 0) {
    printk(KERN_ERR "%s(): slot cannot support VPP %u\n",
	   __func__, state->Vpp);
    return -1;
  }

  ret = sa1111_pcmcia_configure_socket(skt, state);
  if (ret == 0) {
    unsigned long flags;

    local_irq_save(flags);
    sa1111_set_io(SA1111_DEV(skt->dev), pa_dwr_mask, pa_dwr_set);
    local_irq_restore(flags);
  }

  return ret;
}

static struct pcmcia_low_level jornada720_pcmcia_ops = {
  .owner		= THIS_MODULE,
  .hw_init		= jornada720_pcmcia_hw_init,
  .hw_shutdown		= sa1111_pcmcia_hw_shutdown,
  .socket_state		= sa1111_pcmcia_socket_state,
  .configure_socket	= jornada720_pcmcia_configure_socket,

  .socket_init		= sa1111_pcmcia_socket_init,
  .socket_suspend	= sa1111_pcmcia_socket_suspend,
};

int __devinit pcmcia_jornada720_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_jornada720())
		ret = sa11xx_drv_pcmcia_probe(dev, &jornada720_pcmcia_ops, 0, 2);

	return ret;
}
