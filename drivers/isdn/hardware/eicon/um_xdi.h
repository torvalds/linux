/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: um_xdi.h,v 1.1.2.2 2002/10/02 14:38:38 armin Exp $ */

#ifndef __DIVA_USER_MODE_XDI_H__
#define __DIVA_USER_MODE_XDI_H__

/*
  Contains declaratiom of structures shared between application
  and user mode idi driver
*/

typedef struct _diva_um_idi_adapter_features {
	dword type;
	dword features;
	dword channels;
	dword serial_number;
	char name[128];
} diva_um_idi_adapter_features_t;

#define DIVA_UM_IDI_REQ_MASK			0x0000FFFF
#define DIVA_UM_IDI_REQ_TYPE_MASK		(~(DIVA_UM_IDI_REQ_MASK))
#define DIVA_UM_IDI_GET_FEATURES		1	/* trigger features indication */
#define DIVA_UM_IDI_REQ				2
#define DIVA_UM_IDI_REQ_TYPE_MAN		0x10000000
#define DIVA_UM_IDI_REQ_TYPE_SIG		0x20000000
#define DIVA_UM_IDI_REQ_TYPE_NET		0x30000000
#define DIVA_UM_IDI_REQ_MAN			(DIVA_UM_IDI_REQ | DIVA_UM_IDI_REQ_TYPE_MAN)
#define DIVA_UM_IDI_REQ_SIG			(DIVA_UM_IDI_REQ | DIVA_UM_IDI_REQ_TYPE_SIG)
#define DIVA_UM_IDI_REQ_NET			(DIVA_UM_IDI_REQ | DIVA_UM_IDI_REQ_TYPE_NET)
/*
  data_length  bytes will follow this structure
*/
typedef struct _diva_um_idi_req_hdr {
	dword type;
	dword Req;
	dword ReqCh;
	dword data_length;
} diva_um_idi_req_hdr_t;

typedef struct _diva_um_idi_ind_parameters {
	dword Ind;
	dword IndCh;
} diva_um_idi_ind_parameters_t;

typedef struct _diva_um_idi_rc_parameters {
	dword Rc;
	dword RcCh;
} diva_um_idi_rc_parameters_t;

typedef union _diva_um_idi_ind {
	diva_um_idi_adapter_features_t features;
	diva_um_idi_ind_parameters_t ind;
	diva_um_idi_rc_parameters_t rc;
} diva_um_idi_ind_t;

#define DIVA_UM_IDI_IND_FEATURES  1	/* features indication */
#define DIVA_UM_IDI_IND           2
#define DIVA_UM_IDI_IND_RC        3
/*
  data_length bytes of data follow
  this structure
*/
typedef struct _diva_um_idi_ind_hdr {
	dword type;
	diva_um_idi_ind_t hdr;
	dword data_length;
} diva_um_idi_ind_hdr_t;

#endif
