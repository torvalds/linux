/*
 * PCMCIA 16-bit compatibility functions
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Copyright (C) 2004 Dominik Brodowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"

int pcmcia_get_first_tuple(client_handle_t handle, tuple_t *tuple)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_get_first_tuple(s, handle->Function, tuple);
}
EXPORT_SYMBOL(pcmcia_get_first_tuple);

int pcmcia_get_next_tuple(client_handle_t handle, tuple_t *tuple)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_get_next_tuple(s, handle->Function, tuple);
}
EXPORT_SYMBOL(pcmcia_get_next_tuple);

int pcmcia_get_tuple_data(client_handle_t handle, tuple_t *tuple)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_get_tuple_data(s, tuple);
}
EXPORT_SYMBOL(pcmcia_get_tuple_data);

int pcmcia_parse_tuple(client_handle_t handle, tuple_t *tuple, cisparse_t *parse)
{
	return pccard_parse_tuple(tuple, parse);
}
EXPORT_SYMBOL(pcmcia_parse_tuple);

int pcmcia_validate_cis(client_handle_t handle, cisinfo_t *info)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_validate_cis(s, handle->Function, info);
}
EXPORT_SYMBOL(pcmcia_validate_cis);

int pcmcia_get_configuration_info(client_handle_t handle,
				  config_info_t *config)
{
	struct pcmcia_socket *s;

	if ((CHECK_HANDLE(handle)) || !config)
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	if (!s)
		return CS_BAD_HANDLE;
	return pccard_get_configuration_info(s, handle->Function, config);
}
EXPORT_SYMBOL(pcmcia_get_configuration_info);

int pcmcia_reset_card(client_handle_t handle, client_req_t *req)
{
	struct pcmcia_socket *skt;
    
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	skt = SOCKET(handle);
	if (!skt)
		return CS_BAD_HANDLE;

	return pccard_reset_card(skt);
}
EXPORT_SYMBOL(pcmcia_reset_card);

int pcmcia_get_status(client_handle_t handle, cs_status_t *status)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_get_status(s, handle->Function, status);
}
EXPORT_SYMBOL(pcmcia_get_status);

int pcmcia_access_configuration_register(client_handle_t handle,
					 conf_reg_t *reg)
{
	struct pcmcia_socket *s;
	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;
	s = SOCKET(handle);
	return pccard_access_configuration_register(s, handle->Function, reg);
}
EXPORT_SYMBOL(pcmcia_access_configuration_register);

