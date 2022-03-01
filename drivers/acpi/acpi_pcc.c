// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Sudeep Holla <sudeep.holla@arm.com>
 * Copyright 2021 Arm Limited
 *
 * The PCC Address Space also referred as PCC Operation Region pertains to the
 * region of PCC subspace that succeeds the PCC signature. The PCC Operation
 * Region works in conjunction with the PCC Table(Platform Communications
 * Channel Table). PCC subspaces that are marked for use as PCC Operation
 * Regions must not be used as PCC subspaces for the standard ACPI features
 * such as CPPC, RASF, PDTT and MPST. These standard features must always use
 * the PCC Table instead.
 *
 * This driver sets up the PCC Address Space and installs an handler to enable
 * handling of PCC OpRegion in the firmware.
 *
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/io.h>

#include <acpi/pcc.h>

struct pcc_data {
	struct pcc_mbox_chan *pcc_chan;
	void __iomem *pcc_comm_addr;
	struct completion done;
	struct mbox_client cl;
	struct acpi_pcc_info ctx;
};

static struct acpi_pcc_info pcc_ctx;

static void pcc_rx_callback(struct mbox_client *cl, void *m)
{
	struct pcc_data *data = container_of(cl, struct pcc_data, cl);

	complete(&data->done);
}

static acpi_status
acpi_pcc_address_space_setup(acpi_handle region_handle, u32 function,
			     void *handler_context,  void **region_context)
{
	struct pcc_data *data;
	struct acpi_pcc_info *ctx = handler_context;
	struct pcc_mbox_chan *pcc_chan;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return AE_NO_MEMORY;

	data->cl.rx_callback = pcc_rx_callback;
	data->cl.knows_txdone = true;
	data->ctx.length = ctx->length;
	data->ctx.subspace_id = ctx->subspace_id;
	data->ctx.internal_buffer = ctx->internal_buffer;

	init_completion(&data->done);
	data->pcc_chan = pcc_mbox_request_channel(&data->cl, ctx->subspace_id);
	if (IS_ERR(data->pcc_chan)) {
		pr_err("Failed to find PCC channel for subspace %d\n",
		       ctx->subspace_id);
		return AE_NOT_FOUND;
	}

	pcc_chan = data->pcc_chan;
	data->pcc_comm_addr = acpi_os_ioremap(pcc_chan->shmem_base_addr,
					      pcc_chan->shmem_size);
	if (!data->pcc_comm_addr) {
		pr_err("Failed to ioremap PCC comm region mem for %d\n",
		       ctx->subspace_id);
		return AE_NO_MEMORY;
	}

	*region_context = data;
	return AE_OK;
}

static acpi_status
acpi_pcc_address_space_handler(u32 function, acpi_physical_address addr,
			       u32 bits, acpi_integer *value,
			       void *handler_context, void *region_context)
{
	int ret;
	struct pcc_data *data = region_context;

	reinit_completion(&data->done);

	/* Write to Shared Memory */
	memcpy_toio(data->pcc_comm_addr, (void *)value, data->ctx.length);

	ret = mbox_send_message(data->pcc_chan->mchan, NULL);
	if (ret < 0)
		return AE_ERROR;

	if (data->pcc_chan->mchan->mbox->txdone_irq)
		wait_for_completion(&data->done);

	mbox_client_txdone(data->pcc_chan->mchan, ret);

	memcpy_fromio(value, data->pcc_comm_addr, data->ctx.length);

	return AE_OK;
}

void __init acpi_init_pcc(void)
{
	acpi_status status;

	status = acpi_install_address_space_handler(ACPI_ROOT_OBJECT,
						    ACPI_ADR_SPACE_PLATFORM_COMM,
						    &acpi_pcc_address_space_handler,
						    &acpi_pcc_address_space_setup,
						    &pcc_ctx);
	if (ACPI_FAILURE(status))
		pr_alert("OperationRegion handler could not be installed\n");
}
