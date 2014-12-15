/*
 *
 * arch/arm/mach-meson8b/include/mach/am_regs.h
 *
 *  Copyright (C) 2011-2013 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block defintions for core devices like the timer.
 */

#ifndef __MACH_MESSON8B_AVOSSTYL_IO_H
#define __MACH_MESSON8B_AVOSSTYL_IO_H

#ifndef __ASSEMBLY__
#define WRITE_REG(bus,reg, val) aml_write_reg32( bus##_REG_ADDR(reg),val)
#define READ_REG(bus,reg) (aml_read_reg32(bus##_REG_ADDR(reg)))
#define WRITE_REG_BITS(bus,reg, val, start, len) \
    aml_set_reg32_bits(bus##_REG_ADDR(reg),	val,start,len)
#define READ_REG_BITS(bus,reg, start, len) \
     aml_get_reg32_bits(bus##_REG_ADDR(reg),start,len)
#define CLEAR_REG_MASK(bus,reg, mask)   aml_clr_reg32_mask(bus##_REG_ADDR(reg), (mask))
#define SET_REG_MASK(bus,reg, mask)     aml_set_reg32_mask(bus##_REG_ADDR(reg), (mask))

#ifndef CONFIG_DISABLE_VCBUS_IO_FUNC
///VCBUS BUS io operation
#define WRITE_VCBUS_REG(a...)        WRITE_REG(VCBUS,a) 
#define READ_VCBUS_REG(a...)         READ_REG(VCBUS,a)
#define WRITE_VCBUS_REG_BITS(a...)   WRITE_REG_BITS(VCBUS,a)
#define READ_VCBUS_REG_BITS(a...)    READ_REG_BITS(VCBUS,a)
#define CLEAR_VCBUS_REG_MASK(a...)   CLEAR_REG_MASK(VCBUS,a)
#define SET_VCBUS_REG_MASK(a...)     SET_REG_MASK(VCBUS,a)
#endif
#ifndef CONFIG_DISABLE_CBUS_IO_FUNC
///CBUS BUS io operation
#define WRITE_CBUS_REG(a...)        WRITE_REG(CBUS,a) 
#define READ_CBUS_REG(a...)         READ_REG(CBUS,a)
#define WRITE_CBUS_REG_BITS(a...)   WRITE_REG_BITS(CBUS,a)
#define READ_CBUS_REG_BITS(a...)    READ_REG_BITS(CBUS,a)
#define CLEAR_CBUS_REG_MASK(a...)   CLEAR_REG_MASK(CBUS,a)
#define SET_CBUS_REG_MASK(a...)     SET_REG_MASK(CBUS,a)
#endif
#ifndef CONFIG_DISABLE_AXI_IO_FUNC
///AXI BUS io operation
#define WRITE_AXI_REG(a...)         WRITE_REG(AXI,a) 
#define READ_AXI_REG(a...)          READ_REG(AXI,a)
#define WRITE_AXI_REG_BITS(a...)    WRITE_REG_BITS(AXI,a)
#define READ_AXI_REG_BITS(a...)     READ_REG_BITS(AXI,a)
#define CLEAR_AXI_REG_MASK(a...)    CLEAR_REG_MASK(AXI,a)
#define SET_AXI_REG_MASK(a...)      SET_REG_MASK(AXI,a)
#endif
#ifndef CONFIG_DISABLE_AHB_IO_FUNC
///AHB BUS io operation
#define WRITE_AHB_REG(a...)         WRITE_REG(AHB,a) 
#define READ_AHB_REG(a...)          READ_REG(AHB,a)
#define WRITE_AHB_REG_BITS(a...)    WRITE_REG_BITS(AHB,a)
#define READ_AHB_REG_BITS(a...)     READ_REG_BITS(AHB,a)
#define CLEAR_AHB_REG_MASK(a...)    CLEAR_REG_MASK(AHB,a)
#define SET_AHB_REG_MASK(a...)      SET_REG_MASK(AHB,a)
#endif
#ifndef CONFIG_DISABLE_APB_IO_FUNC
///APB BUS io operation
#define WRITE_APB_REG(a...)         WRITE_REG(APB,a) 
#define READ_APB_REG(a...)          READ_REG(APB,a)
#define WRITE_APB_REG_BITS(a...)    WRITE_REG_BITS(APB,a)
#define READ_APB_REG_BITS(a...)     READ_REG_BITS(APB,a)
#define CLEAR_APB_REG_MASK(a...)    CLEAR_REG_MASK(APB,a)
#define SET_APB_REG_MASK(a...)      SET_REG_MASK(APB,a)
#endif

#ifndef CONFIG_DISABLE_MMC_IO_FUNC
///APB BUS io operation
#define WRITE_MMC_REG(a...)         WRITE_REG(MMC,a) 
#define READ_MMC_REG(a...)          READ_REG(MMC,a)
#define WRITE_MMC_REG_BITS(a...)    WRITE_REG_BITS(MMC,a)
#define READ_MMC_REG_BITS(a...)     READ_REG_BITS(MMC,a)
#define CLEAR_MMC_REG_MASK(a...)    CLEAR_REG_MASK(MMC,a)
#define SET_MMC_REG_MASK(a...)      SET_REG_MASK(MMC,a)
#endif

#ifndef CONFIG_DISABLE_AOBUS_IO_FUNC
///AOBUS BUS io operation
#define WRITE_AOBUS_REG(a...)         WRITE_REG(AOBUS,a) 
#define READ_AOBUS_REG(a...)          READ_REG(AOBUS,a)
#define WRITE_AOBUS_REG_BITS(a...)    WRITE_REG_BITS(AOBUS,a)
#define READ_AOBUS_REG_BITS(a...)     READ_REG_BITS(AOBUS,a)
#define CLEAR_AOBUS_REG_MASK(a...)    CLEAR_REG_MASK(AOBUS,a)
#define SET_AOBUS_REG_MASK(a...)      SET_REG_MASK(AOBUS,a)
#endif
#ifndef CONFIG_DISABLE_MPEG_IO_FUNC
///MPEG BUS io operation
#define WRITE_MPEG_REG      WRITE_CBUS_REG 
#define READ_MPEG_REG       READ_CBUS_REG
#define WRITE_MPEG_REG_BITS WRITE_CBUS_REG_BITS
#define READ_MPEG_REG_BITS  READ_CBUS_REG_BITS
#define CLEAR_MPEG_REG_MASK CLEAR_CBUS_REG_MASK
#define SET_MPEG_REG_MASK   SET_CBUS_REG_MASK
#endif


#endif



#endif //__MACH_MESSON8_REGS_H
