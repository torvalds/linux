#include <linux/module.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <mach/mod_gate.h>

unsigned char GCLK_ref[GCLK_IDX_MAX];
EXPORT_SYMBOL(GCLK_ref);


int  video_dac_enable(unsigned char enable_mask)
{
    switch_mod_gate_by_name("venc", 1);
    CLEAR_CBUS_REG_MASK(VENC_VDAC_SETTING, enable_mask & 0x1f);
    return 0;
}
EXPORT_SYMBOL(video_dac_enable);

int  video_dac_disable()
{
    SET_CBUS_REG_MASK(VENC_VDAC_SETTING, 0x1f);
    switch_mod_gate_by_name("venc", 0);
  
    return 0;
}
EXPORT_SYMBOL(video_dac_disable);


