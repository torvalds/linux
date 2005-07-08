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
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"

int pcmcia_get_first_tuple(struct pcmcia_device *p_dev, tuple_t *tuple)
{
	return pccard_get_first_tuple(p_dev->socket, p_dev->func, tuple);
}
EXPORT_SYMBOL(pcmcia_get_first_tuple);

int pcmcia_get_next_tuple(struct pcmcia_device *p_dev, tuple_t *tuple)
{
	return pccard_get_next_tuple(p_dev->socket, p_dev->func, tuple);
}
EXPORT_SYMBOL(pcmcia_get_next_tuple);

int pcmcia_get_tuple_data(struct pcmcia_device *p_dev, tuple_t *tuple)
{
	return pccard_get_tuple_data(p_dev->socket, tuple);
}
EXPORT_SYMBOL(pcmcia_get_tuple_data);

int pcmcia_parse_tuple(struct pcmcia_device *p_dev, tuple_t *tuple, cisparse_t *parse)
{
	return pccard_parse_tuple(tuple, parse);
}
EXPORT_SYMBOL(pcmcia_parse_tuple);

int pcmcia_validate_cis(struct pcmcia_device *p_dev, cisinfo_t *info)
{
	return pccard_validate_cis(p_dev->socket, p_dev->func, info);
}
EXPORT_SYMBOL(pcmcia_validate_cis);


int pcmcia_reset_card(struct pcmcia_device *p_dev, client_req_t *req)
{
	return pccard_reset_card(p_dev->socket);
}
EXPORT_SYMBOL(pcmcia_reset_card);
