/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  bfa_log.h BFA log library data structure and function definition
 */

#ifndef __BFA_LOG_H__
#define __BFA_LOG_H__

#include <bfa_os_inc.h>
#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_aen.h>

/*
 * BFA log module definition
 *
 * To create a new module id:
 * Add a #define at the end of the list below. Select a value for your
 * definition so that it is one (1) greater than the previous
 * definition. Modify the definition of BFA_LOG_MODULE_ID_MAX to become
 * your new definition.
 * Should have no gaps in between the values because this is used in arrays.
 * IMPORTANT: AEN_IDs must be at the begining, otherwise update bfa_defs_aen.h
 */

enum bfa_log_module_id {
	BFA_LOG_UNUSED_ID	= 0,

	/* AEN defs begin */
	BFA_LOG_AEN_MIN		= BFA_LOG_UNUSED_ID,

	BFA_LOG_AEN_ID_ADAPTER 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_ADAPTER,/* 1 */
	BFA_LOG_AEN_ID_PORT 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_PORT,	/* 2 */
	BFA_LOG_AEN_ID_LPORT 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_LPORT,	/* 3 */
	BFA_LOG_AEN_ID_RPORT 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_RPORT,	/* 4 */
	BFA_LOG_AEN_ID_ITNIM 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_ITNIM,	/* 5 */
	BFA_LOG_AEN_ID_TIN 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_TIN,	/* 6 */
	BFA_LOG_AEN_ID_IPFC 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_IPFC,	/* 7 */
	BFA_LOG_AEN_ID_AUDIT 	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_AUDIT,	/* 8 */
	BFA_LOG_AEN_ID_IOC	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_IOC,	/* 9 */
	BFA_LOG_AEN_ID_ETHPORT	= BFA_LOG_AEN_MIN + BFA_AEN_CAT_ETHPORT,/* 10 */

	BFA_LOG_AEN_MAX		= BFA_LOG_AEN_ID_ETHPORT,
	/* AEN defs end */

	BFA_LOG_MODULE_ID_MIN	= BFA_LOG_AEN_MAX,

	BFA_LOG_FW_ID		= BFA_LOG_MODULE_ID_MIN + 1,
	BFA_LOG_HAL_ID		= BFA_LOG_MODULE_ID_MIN + 2,
	BFA_LOG_FCS_ID		= BFA_LOG_MODULE_ID_MIN + 3,
	BFA_LOG_WDRV_ID		= BFA_LOG_MODULE_ID_MIN + 4,
	BFA_LOG_LINUX_ID	= BFA_LOG_MODULE_ID_MIN + 5,
	BFA_LOG_SOLARIS_ID	= BFA_LOG_MODULE_ID_MIN + 6,

	BFA_LOG_MODULE_ID_MAX 	= BFA_LOG_SOLARIS_ID,

	/* Not part of any arrays */
	BFA_LOG_MODULE_ID_ALL 	= BFA_LOG_MODULE_ID_MAX + 1,
	BFA_LOG_AEN_ALL 	= BFA_LOG_MODULE_ID_MAX + 2,
	BFA_LOG_DRV_ALL		= BFA_LOG_MODULE_ID_MAX + 3,
};

/*
 * BFA log catalog name
 */
#define BFA_LOG_CAT_NAME	"BFA"

/*
 * bfa log severity values
 */
enum bfa_log_severity {
	BFA_LOG_INVALID = 0,
	BFA_LOG_CRITICAL = 1,
	BFA_LOG_ERROR = 2,
	BFA_LOG_WARNING = 3,
	BFA_LOG_INFO = 4,
	BFA_LOG_NONE = 5,
	BFA_LOG_LEVEL_MAX = BFA_LOG_NONE
};

#define BFA_LOG_MODID_OFFSET		16


struct bfa_log_msgdef_s {
	u32	msg_id;		/*  message id */
	int		attributes;	/*  attributes */
	int		severity;	/*  severity level */
	char		*msg_value;
					/*  msg string */
	char		*message;
					/*  msg format string */
	int		arg_type;	/*  argument type */
	int		arg_num;	/*  number of argument */
};

/*
 * supported argument type
 */
enum bfa_log_arg_type {
	BFA_LOG_S = 0,		/*  string */
	BFA_LOG_D,		/*  decimal */
	BFA_LOG_I,		/*  integer */
	BFA_LOG_O,		/*  oct number */
	BFA_LOG_U,		/*  unsigned integer */
	BFA_LOG_X,		/*  hex number */
	BFA_LOG_F,		/*  floating */
	BFA_LOG_C,		/*  character */
	BFA_LOG_L,		/*  double */
	BFA_LOG_P		/*  pointer */
};

#define BFA_LOG_ARG_TYPE	2
#define BFA_LOG_ARG0		(0 * BFA_LOG_ARG_TYPE)
#define BFA_LOG_ARG1		(1 * BFA_LOG_ARG_TYPE)
#define BFA_LOG_ARG2		(2 * BFA_LOG_ARG_TYPE)
#define BFA_LOG_ARG3		(3 * BFA_LOG_ARG_TYPE)

#define BFA_LOG_GET_MOD_ID(msgid) ((msgid >> BFA_LOG_MODID_OFFSET) & 0xff)
#define BFA_LOG_GET_MSG_IDX(msgid) (msgid & 0xffff)
#define BFA_LOG_GET_MSG_ID(msgdef) ((msgdef)->msg_id)
#define BFA_LOG_GET_MSG_FMT_STRING(msgdef) ((msgdef)->message)
#define BFA_LOG_GET_SEVERITY(msgdef) ((msgdef)->severity)

/*
 * Event attributes
 */
#define BFA_LOG_ATTR_NONE	0
#define BFA_LOG_ATTR_AUDIT	1
#define BFA_LOG_ATTR_LOG	2
#define BFA_LOG_ATTR_FFDC	4

#define BFA_LOG_CREATE_ID(msw, lsw) \
	(((u32)msw << BFA_LOG_MODID_OFFSET) | lsw)

struct bfa_log_mod_s;

/**
 * callback function
 */
typedef void (*bfa_log_cb_t)(struct bfa_log_mod_s *log_mod, u32 msg_id,
			const char *format, ...);


struct bfa_log_mod_s {
	char		instance_info[BFA_STRING_32];	/*  instance info */
	int		log_level[BFA_LOG_MODULE_ID_MAX + 1];
						/*  log level for modules */
	bfa_log_cb_t	cbfn; 			/*  callback function */
};

extern int bfa_log_init(struct bfa_log_mod_s *log_mod,
			char *instance_name, bfa_log_cb_t cbfn);
extern int bfa_log(struct bfa_log_mod_s *log_mod, u32 msg_id, ...);
extern bfa_status_t bfa_log_set_level(struct bfa_log_mod_s *log_mod,
			int mod_id, enum bfa_log_severity log_level);
extern bfa_status_t bfa_log_set_level_all(struct bfa_log_mod_s *log_mod,
			enum bfa_log_severity log_level);
extern bfa_status_t bfa_log_set_level_aen(struct bfa_log_mod_s *log_mod,
			enum bfa_log_severity log_level);
extern enum bfa_log_severity bfa_log_get_level(struct bfa_log_mod_s *log_mod,
			int mod_id);
extern enum bfa_log_severity bfa_log_get_msg_level(
			struct bfa_log_mod_s *log_mod, u32 msg_id);
/*
 * array of messages generated from xml files
 */
extern struct bfa_log_msgdef_s bfa_log_msg_array[];

#endif
