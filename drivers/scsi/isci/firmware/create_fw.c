#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include <strings.h>
#include <stdint.h>

#include "create_fw.h"
#include "../probe_roms.h"

int write_blob(struct isci_orom *isci_orom)
{
	FILE *fd;
	int err;
	size_t count;

	fd = fopen(blob_name, "w+");
	if (!fd) {
		perror("Open file for write failed");
		fclose(fd);
		return -EIO;
	}

	count = fwrite(isci_orom, sizeof(struct isci_orom), 1, fd);
	if (count != 1) {
		perror("Write data failed");
		fclose(fd);
		return -EIO;
	}

	fclose(fd);

	return 0;
}

void set_binary_values(struct isci_orom *isci_orom)
{
	int ctrl_idx, phy_idx, port_idx;

	/* setting OROM signature */
	strncpy(isci_orom->hdr.signature, sig, strlen(sig));
	isci_orom->hdr.version = version;
	isci_orom->hdr.total_block_length = sizeof(struct isci_orom);
	isci_orom->hdr.hdr_length = sizeof(struct sci_bios_oem_param_block_hdr);
	isci_orom->hdr.num_elements = num_elements;

	for (ctrl_idx = 0; ctrl_idx < 2; ctrl_idx++) {
		isci_orom->ctrl[ctrl_idx].controller.mode_type = mode_type;
		isci_orom->ctrl[ctrl_idx].controller.max_concurrent_dev_spin_up =
			max_num_concurrent_dev_spin_up;
		isci_orom->ctrl[ctrl_idx].controller.do_enable_ssc =
			enable_ssc;

		for (port_idx = 0; port_idx < 4; port_idx++)
			isci_orom->ctrl[ctrl_idx].ports[port_idx].phy_mask =
				phy_mask[ctrl_idx][port_idx];

		for (phy_idx = 0; phy_idx < 4; phy_idx++) {
			isci_orom->ctrl[ctrl_idx].phys[phy_idx].sas_address.high =
				(__u32)(sas_addr[ctrl_idx][phy_idx] >> 32);
			isci_orom->ctrl[ctrl_idx].phys[phy_idx].sas_address.low =
				(__u32)(sas_addr[ctrl_idx][phy_idx]);

			isci_orom->ctrl[ctrl_idx].phys[phy_idx].afe_tx_amp_control0 =
				afe_tx_amp_control0;
			isci_orom->ctrl[ctrl_idx].phys[phy_idx].afe_tx_amp_control1 =
				afe_tx_amp_control1;
			isci_orom->ctrl[ctrl_idx].phys[phy_idx].afe_tx_amp_control2 =
				afe_tx_amp_control2;
			isci_orom->ctrl[ctrl_idx].phys[phy_idx].afe_tx_amp_control3 =
				afe_tx_amp_control3;
		}
	}
}

int main(void)
{
	int err;
	struct isci_orom *isci_orom;

	isci_orom = malloc(sizeof(struct isci_orom));
	memset(isci_orom, 0, sizeof(struct isci_orom));

	set_binary_values(isci_orom);

	err = write_blob(isci_orom);
	if (err < 0) {
		free(isci_orom);
		return err;
	}

	free(isci_orom);
	return 0;
}
