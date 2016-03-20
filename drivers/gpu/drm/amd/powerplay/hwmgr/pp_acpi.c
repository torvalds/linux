#include <linux/errno.h>
#include "linux/delay.h"
#include "hwmgr.h"
#include "amd_acpi.h"

bool acpi_atcs_functions_supported(void *device, uint32_t index)
{
	int32_t result;
	struct atcs_verify_interface output_buf = {0};

	int32_t temp_buffer = 1;

	result = cgs_call_acpi_method(device, CGS_ACPI_METHOD_ATCS,
						ATCS_FUNCTION_VERIFY_INTERFACE,
						&temp_buffer,
						&output_buf,
						1,
						sizeof(temp_buffer),
						sizeof(output_buf));

	return result == 0 ? (output_buf.function_bits & (1 << (index - 1))) != 0 : false;
}

int acpi_pcie_perf_request(void *device, uint8_t perf_req, bool advertise)
{
	struct atcs_pref_req_input atcs_input;
	struct atcs_pref_req_output atcs_output;
	u32 retry = 3;
	int result;
	struct cgs_system_info info = {0};

	if (!acpi_atcs_functions_supported(device, ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST))
		return -EINVAL;

	info.size = sizeof(struct cgs_system_info);
	info.info_id = CGS_SYSTEM_INFO_ADAPTER_BDF_ID;
	result = cgs_query_system_info(device, &info);
	if (result != 0)
		return -EINVAL;
	atcs_input.client_id = (uint16_t)info.value;
	atcs_input.size = sizeof(struct atcs_pref_req_input);
	atcs_input.valid_flags_mask = ATCS_VALID_FLAGS_MASK;
	atcs_input.flags = ATCS_WAIT_FOR_COMPLETION;
	if (advertise)
		atcs_input.flags |= ATCS_ADVERTISE_CAPS;
	atcs_input.req_type = ATCS_PCIE_LINK_SPEED;
	atcs_input.perf_req = perf_req;

	atcs_output.size = sizeof(struct atcs_pref_req_input);

	while (retry--) {
		result = cgs_call_acpi_method(device,
						CGS_ACPI_METHOD_ATCS,
						ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST,
						&atcs_input,
						&atcs_output,
						0,
						sizeof(atcs_input),
						sizeof(atcs_output));
		if (result != 0)
			return -EIO;

		switch (atcs_output.ret_val) {
		case ATCS_REQUEST_REFUSED:
		default:
			return -EINVAL;
		case ATCS_REQUEST_COMPLETE:
			return 0;
		case ATCS_REQUEST_IN_PROGRESS:
			udelay(10);
			break;
		}
	}

	return 0;
}
