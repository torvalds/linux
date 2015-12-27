/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRM_TEGRA_DPAUX_H
#define DRM_TEGRA_DPAUX_H

#define DPAUX_CTXSW 0x00

#define DPAUX_INTR_EN_AUX 0x01
#define DPAUX_INTR_AUX 0x05
#define DPAUX_INTR_AUX_DONE (1 << 3)
#define DPAUX_INTR_IRQ_EVENT (1 << 2)
#define DPAUX_INTR_UNPLUG_EVENT (1 << 1)
#define DPAUX_INTR_PLUG_EVENT (1 << 0)

#define DPAUX_DP_AUXDATA_WRITE(x) (0x09 + ((x) << 2))
#define DPAUX_DP_AUXDATA_READ(x) (0x19 + ((x) << 2))
#define DPAUX_DP_AUXADDR 0x29

#define DPAUX_DP_AUXCTL 0x2d
#define DPAUX_DP_AUXCTL_TRANSACTREQ (1 << 16)
#define DPAUX_DP_AUXCTL_CMD_AUX_RD (9 << 12)
#define DPAUX_DP_AUXCTL_CMD_AUX_WR (8 << 12)
#define DPAUX_DP_AUXCTL_CMD_MOT_RQ (6 << 12)
#define DPAUX_DP_AUXCTL_CMD_MOT_RD (5 << 12)
#define DPAUX_DP_AUXCTL_CMD_MOT_WR (4 << 12)
#define DPAUX_DP_AUXCTL_CMD_I2C_RQ (2 << 12)
#define DPAUX_DP_AUXCTL_CMD_I2C_RD (1 << 12)
#define DPAUX_DP_AUXCTL_CMD_I2C_WR (0 << 12)
#define DPAUX_DP_AUXCTL_CMD_ADDRESS_ONLY (1 << 8)
#define DPAUX_DP_AUXCTL_CMDLEN(x) ((x) & 0xff)

#define DPAUX_DP_AUXSTAT 0x31
#define DPAUX_DP_AUXSTAT_HPD_STATUS (1 << 28)
#define DPAUX_DP_AUXSTAT_REPLY_TYPE_MASK (0xf0000)
#define DPAUX_DP_AUXSTAT_NO_STOP_ERROR (1 << 11)
#define DPAUX_DP_AUXSTAT_SINKSTAT_ERROR (1 << 10)
#define DPAUX_DP_AUXSTAT_RX_ERROR (1 << 9)
#define DPAUX_DP_AUXSTAT_TIMEOUT_ERROR (1 << 8)
#define DPAUX_DP_AUXSTAT_REPLY_MASK (0xff)

#define DPAUX_DP_AUX_SINKSTAT_LO 0x35
#define DPAUX_DP_AUX_SINKSTAT_HI 0x39

#define DPAUX_HPD_CONFIG 0x3d
#define DPAUX_HPD_CONFIG_UNPLUG_MIN_TIME(x) (((x) & 0xffff) << 16)
#define DPAUX_HPD_CONFIG_PLUG_MIN_TIME(x) ((x) & 0xffff)

#define DPAUX_HPD_IRQ_CONFIG 0x41
#define DPAUX_HPD_IRQ_CONFIG_MIN_LOW_TIME(x) ((x) & 0xffff)

#define DPAUX_DP_AUX_CONFIG 0x45

#define DPAUX_HYBRID_PADCTL 0x49
#define DPAUX_HYBRID_PADCTL_I2C_SDA_INPUT_RCV (1 << 15)
#define DPAUX_HYBRID_PADCTL_I2C_SCL_INPUT_RCV (1 << 14)
#define DPAUX_HYBRID_PADCTL_AUX_CMH(x) (((x) & 0x3) << 12)
#define DPAUX_HYBRID_PADCTL_AUX_DRVZ(x) (((x) & 0x7) << 8)
#define DPAUX_HYBRID_PADCTL_AUX_DRVI(x) (((x) & 0x3f) << 2)
#define DPAUX_HYBRID_PADCTL_AUX_INPUT_RCV (1 << 1)
#define DPAUX_HYBRID_PADCTL_MODE_I2C (1 << 0)
#define DPAUX_HYBRID_PADCTL_MODE_AUX (0 << 0)

#define DPAUX_HYBRID_SPARE 0x4d
#define DPAUX_HYBRID_SPARE_PAD_POWER_DOWN (1 << 0)

#define DPAUX_SCRATCH_REG0 0x51
#define DPAUX_SCRATCH_REG1 0x55
#define DPAUX_SCRATCH_REG2 0x59

#endif
