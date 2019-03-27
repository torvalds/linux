/*	$NetBSD: scsitest.c,v 1.2 2014/04/25 00:24:39 pooka Exp $	*/

/*
 * Copyright (c) 2010 Antti Kantee.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * A SCSI target which is useful for debugging our scsipi driver stack.
 * Currently it pretends to be a single CD.
 *
 * Freely add the necessary features for your tests.  Just remember to
 * run the atf test suite to make sure you didn't cause regressions to
 * other tests.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: scsitest.c,v 1.2 2014/04/25 00:24:39 pooka Exp $");

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>

#include <dev/scsipi/scsiconf.h>
#include <dev/scsipi/scsipiconf.h>
#include <dev/scsipi/scsi_disk.h>
#include <dev/scsipi/scsipi_cd.h>
#include <dev/scsipi/scsipi_all.h>

#include <rump/rumpuser.h>

#include "scsitest.h"

int	scsitest_match(device_t, cfdata_t, void *);
void	scsitest_attach(device_t, device_t, void *);

struct scsitest {
	struct scsipi_channel sc_channel;
	struct scsipi_adapter sc_adapter;
};

CFATTACH_DECL_NEW(scsitest, sizeof(struct scsitest), scsitest_match,
	scsitest_attach, NULL, NULL);

/*
 * tosi.iso can be used to deliver CD requests to a host file with the
 * name in USE_TOSI_ISO (yes, it's extrasimplistic).
 */
//#define USE_TOSI_ISO

#define CDBLOCKSIZE 2048
static uint32_t mycdsize = 2048;
static int isofd;

#define MYCDISO "tosi.iso"

unsigned rump_scsitest_err[RUMP_SCSITEST_MAXERROR];

static void
sense_notready(struct scsipi_xfer *xs)
{
	struct scsi_sense_data *sense = &xs->sense.scsi_sense;

	xs->error = XS_SENSE;

	sense->response_code = 0x70;
	sense->flags = SKEY_NOT_READY;
	sense->asc = 0x3A;
	sense->ascq = 0x00;
	sense->extra_len = 6;
}

/*
 * This is pretty much a CD target for now
 */
static void
scsitest_request(struct scsipi_channel *chan,
	scsipi_adapter_req_t req, void *arg)
{
	struct scsipi_xfer *xs = arg;
	struct scsipi_generic *cmd = xs->cmd;
#ifdef USE_TOSI_ISO
	int error;
#endif

	if (req != ADAPTER_REQ_RUN_XFER)
		return;

	//show_scsipi_xs(xs);

	switch (cmd->opcode) {
	case SCSI_TEST_UNIT_READY:
		if (isofd == -1)
			sense_notready(xs);

		break;
	case INQUIRY: {
		struct scsipi_inquiry_data *inqbuf = (void *)xs->data;

		memset(inqbuf, 0, sizeof(*inqbuf));
		inqbuf->device = T_CDROM;
		inqbuf->dev_qual2 = SID_REMOVABLE;
		strcpy(inqbuf->vendor, "RUMPHOBO");
		strcpy(inqbuf->product, "It's a LIE");
		strcpy(inqbuf->revision, "0.00");
		break;
	}
	case READ_CD_CAPACITY: {
		struct scsipi_read_cd_cap_data *ret = (void *)xs->data;

		_lto4b(CDBLOCKSIZE, ret->length);
		_lto4b(mycdsize, ret->addr);

		break;
	}
	case READ_DISCINFO: {
		struct scsipi_read_discinfo_data *ret = (void *)xs->data;

		memset(ret, 0, sizeof(*ret));
		break;
	}
	case READ_TRACKINFO: {
		struct scsipi_read_trackinfo_data *ret = (void *)xs->data;

		_lto4b(mycdsize, ret->track_size);
		break;
	}
	case READ_TOC: {
		struct scsipi_toc_header *ret = (void *)xs->data;

		memset(ret, 0, sizeof(*ret));
		break;
	}
	case START_STOP: {
		struct scsipi_start_stop *param = (void *)cmd;

		if (param->how & SSS_LOEJ) {
#ifdef USE_TOSI_ISO
			rumpuser_close(isofd, &error);
#endif
			isofd = -1;
		}
		break;
	}
	case SCSI_SYNCHRONIZE_CACHE_10: {
		if (isofd == -1) {
			if ((xs->xs_control & XS_CTL_SILENT) == 0)
				atomic_inc_uint(&rump_scsitest_err
				    [RUMP_SCSITEST_NOISYSYNC]);
			
			sense_notready(xs);
		}

		break;
	}
	case GET_CONFIGURATION: {
		memset(xs->data, 0, sizeof(struct scsipi_get_conf_data));
		break;
	}
	case SCSI_READ_6_COMMAND: {
#ifdef USE_TOSI_ISO
		struct scsi_rw_6 *param = (void *)cmd;

		printf("reading %d bytes from %d\n",
		    param->length * CDBLOCKSIZE,
		    _3btol(param->addr) * CDBLOCKSIZE);
		rumpuser_pread(isofd, xs->data,
		     param->length * CDBLOCKSIZE,
		     _3btol(param->addr) * CDBLOCKSIZE,
		     &error);
#endif

		break;
	}
	case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
		/* hardcoded for now */
		break;
	default:
		printf("unhandled opcode 0x%x\n", cmd->opcode);
		break;
	}

	scsipi_done(xs);
}

int
scsitest_match(device_t parent, cfdata_t match, void *aux)
{
#ifdef USE_TOSI_ISO
	uint64_t fsize;
	int error, ft;

	if (rumpuser_getfileinfo(MYCDISO, &fsize, &ft, &error))
		return 0;
	if (ft != RUMPUSER_FT_REG)
		return 0;
	mycdsize = fsize / CDBLOCKSIZE;

	if ((isofd = rumpuser_open(MYCDISO, RUMPUSER_OPEN_RDWR, &error)) == -1)
		return 0;
#else
	/*
	 * We pretend to have a medium present initially, so != -1.
	 */
	isofd = -2;
#endif

	return 1;
}

void
scsitest_attach(device_t parent, device_t self, void *aux)
{
	struct scsitest *sc = device_private(self);
	
	aprint_naive("\n");
	aprint_normal("\n");

	memset(&sc->sc_adapter, 0, sizeof(sc->sc_adapter));
	sc->sc_adapter.adapt_nchannels = 1;
	sc->sc_adapter.adapt_request = scsitest_request;
	sc->sc_adapter.adapt_minphys = minphys;
	sc->sc_adapter.adapt_dev = self;
	sc->sc_adapter.adapt_max_periph = 1;
	sc->sc_adapter.adapt_openings = 1;

	memset(&sc->sc_channel, 0, sizeof(sc->sc_channel));
	sc->sc_channel.chan_bustype = &scsi_bustype;
	sc->sc_channel.chan_ntargets = 2;
	sc->sc_channel.chan_nluns = 1;
	sc->sc_channel.chan_id = 0;
	sc->sc_channel.chan_flags = SCSIPI_CHAN_NOSETTLE;
	sc->sc_channel.chan_adapter = &sc->sc_adapter;

	config_found_ia(self, "scsi", &sc->sc_channel, scsiprint);
}
