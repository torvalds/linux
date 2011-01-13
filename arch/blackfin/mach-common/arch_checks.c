/*
 * Do some checking to make sure things are OK
 *
 * Copyright 2007-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <asm/fixed_code.h>
#include <mach/anomaly.h>
#include <asm/clocks.h>

#ifdef CONFIG_BFIN_KERNEL_CLOCK

# if (CONFIG_VCO_HZ > CONFIG_MAX_VCO_HZ)
#  error "VCO selected is more than maximum value. Please change the VCO multipler"
# endif

# if (CONFIG_SCLK_HZ > CONFIG_MAX_SCLK_HZ)
# error "Sclk value selected is more than maximum. Please select a proper value for SCLK multiplier"
# endif

# if (CONFIG_SCLK_HZ < CONFIG_MIN_SCLK_HZ)
# error "Sclk value selected is less than minimum. Please select a proper value for SCLK multiplier"
# endif

# if (ANOMALY_05000273) && (CONFIG_SCLK_HZ * 2 > CONFIG_CCLK_HZ)
# error "ANOMALY 05000273, please make sure CCLK is at least 2x SCLK"
# endif

# if (CONFIG_SCLK_HZ > CONFIG_CCLK_HZ) && (CONFIG_SCLK_HZ != CONFIG_CLKIN_HZ) && (CONFIG_CCLK_HZ != CONFIG_CLKIN_HZ)
# error "Please select sclk less than cclk"
# endif

#endif /* CONFIG_BFIN_KERNEL_CLOCK */

#if CONFIG_BOOT_LOAD < FIXED_CODE_END
# error "The kernel load address must be after the fixed code section"
#endif

#if (CONFIG_BOOT_LOAD & 0x3)
# error "The kernel load address must be 4 byte aligned"
#endif

/* The entire kernel must be able to make a 24bit pcrel call to start of L1 */
#if ((0xffffffff - L1_CODE_START + 1) + CONFIG_BOOT_LOAD) > 0x1000000
# error "The kernel load address is too high; keep it below 10meg for safety"
#endif

#if ANOMALY_05000263 && defined(CONFIG_MPU)
# error the MPU will not function safely while Anomaly 05000263 applies
#endif

#if ANOMALY_05000448
# error You are using a part with anomaly 05000448, this issue causes random memory read/write failures - that means random crashes.
#endif

/* if 220 exists, can not set External Memory WB and L2 not_cached, either External Memory not_cached and L2 WB */
#if ANOMALY_05000220 && \
	(defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK))
# error "Anomaly 05000220 does not allow you to use Write Back cache with L2 or External Memory"
#endif

#if ANOMALY_05000491 && !defined(CONFIG_CACHE_FLUSH_L1)
# error You need IFLUSH in L1 inst while Anomaly 05000491 applies
#endif
