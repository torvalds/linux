#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amipcmcia.h>

extern volatile u_short amiga_audio_min_period;
extern u_short amiga_audio_period;

/*
 * Add things here when you find the need for it.
 */
EXPORT_SYMBOL(amiga_model);
EXPORT_SYMBOL(amiga_chipset);
EXPORT_SYMBOL(amiga_hw_present);
EXPORT_SYMBOL(amiga_eclock);
EXPORT_SYMBOL(amiga_colorclock);
EXPORT_SYMBOL(amiga_chip_alloc);
EXPORT_SYMBOL(amiga_chip_free);
EXPORT_SYMBOL(amiga_chip_avail);
EXPORT_SYMBOL(amiga_chip_size);
EXPORT_SYMBOL(amiga_audio_period);
EXPORT_SYMBOL(amiga_audio_min_period);

#ifdef CONFIG_AMIGA_PCMCIA
  EXPORT_SYMBOL(pcmcia_reset);
  EXPORT_SYMBOL(pcmcia_copy_tuple);
  EXPORT_SYMBOL(pcmcia_program_voltage);
  EXPORT_SYMBOL(pcmcia_access_speed);
  EXPORT_SYMBOL(pcmcia_write_enable);
  EXPORT_SYMBOL(pcmcia_write_disable);
#endif
