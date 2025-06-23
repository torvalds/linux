/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Management Complex (MC) userspace public interface
 *
 * Copyright 2021 NXP
 *
 */
#ifndef _FSL_MC_H_
#define _FSL_MC_H_

#include <linux/types.h>

#define MC_CMD_NUM_OF_PARAMS	7

/**
 * struct fsl_mc_command - Management Complex (MC) command structure
 * @header: MC command header
 * @params: MC command parameters
 *
 * Used by FSL_MC_SEND_MC_COMMAND
 */
struct fsl_mc_command {
	__le64 header;
	__le64 params[MC_CMD_NUM_OF_PARAMS];
};

#define FSL_MC_SEND_CMD_IOCTL_TYPE	'R'
#define FSL_MC_SEND_CMD_IOCTL_SEQ	0xE0

#define FSL_MC_SEND_MC_COMMAND \
	_IOWR(FSL_MC_SEND_CMD_IOCTL_TYPE, FSL_MC_SEND_CMD_IOCTL_SEQ, \
	struct fsl_mc_command)

#endif /* _FSL_MC_H_ */
