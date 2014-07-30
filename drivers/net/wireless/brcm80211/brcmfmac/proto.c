/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


 #include <linux/types.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include <brcmu_wifi.h>
#include "dhd.h"
#include "dhd_dbg.h"
#include "proto.h"
#include "bcdc.h"


int brcmf_proto_attach(struct brcmf_pub *drvr)
{
	struct brcmf_proto *proto;

	brcmf_dbg(TRACE, "Enter\n");

	proto = kzalloc(sizeof(*proto), GFP_ATOMIC);
	if (!proto)
		goto fail;

	drvr->proto = proto;
	/* BCDC protocol is only protocol supported for the moment */
	if (brcmf_proto_bcdc_attach(drvr))
		goto fail;

	if ((proto->txdata == NULL) || (proto->hdrpull == NULL) ||
	    (proto->query_dcmd == NULL) || (proto->set_dcmd == NULL) ||
	    (proto->configure_addr_mode == NULL) ||
	    (proto->delete_peer == NULL)) {
		brcmf_err("Not all proto handlers have been installed\n");
		goto fail;
	}
	return 0;

fail:
	kfree(proto);
	drvr->proto = NULL;
	return -ENOMEM;
}

void brcmf_proto_detach(struct brcmf_pub *drvr)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (drvr->proto) {
		brcmf_proto_bcdc_detach(drvr);
		kfree(drvr->proto);
		drvr->proto = NULL;
	}
}
