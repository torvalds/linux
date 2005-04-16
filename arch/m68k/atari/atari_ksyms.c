#include <linux/module.h>

#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atarikb.h>
#include <asm/atari_joystick.h>
#include <asm/atari_stdma.h>
#include <asm/atari_stram.h>

extern void atari_microwire_cmd( int cmd );
extern int atari_MFP_init_done;
extern int atari_SCC_init_done;
extern int atari_SCC_reset_done;

EXPORT_SYMBOL(atari_mch_cookie);
EXPORT_SYMBOL(atari_mch_type);
EXPORT_SYMBOL(atari_hw_present);
EXPORT_SYMBOL(atari_switches);
EXPORT_SYMBOL(atari_dont_touch_floppy_select);
EXPORT_SYMBOL(atari_register_vme_int);
EXPORT_SYMBOL(atari_unregister_vme_int);
EXPORT_SYMBOL(stdma_lock);
EXPORT_SYMBOL(stdma_release);
EXPORT_SYMBOL(stdma_others_waiting);
EXPORT_SYMBOL(stdma_islocked);
EXPORT_SYMBOL(atari_stram_alloc);
EXPORT_SYMBOL(atari_stram_free);

EXPORT_SYMBOL(atari_MFP_init_done);
EXPORT_SYMBOL(atari_SCC_init_done);
EXPORT_SYMBOL(atari_SCC_reset_done);

EXPORT_SYMBOL(atari_microwire_cmd);
