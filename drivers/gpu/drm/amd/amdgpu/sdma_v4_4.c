/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "amdgpu.h"
#include "sdma/sdma_4_4_0_offset.h"
#include "sdma/sdma_4_4_0_sh_mask.h"
#include "soc15.h"
#include "amdgpu_ras.h"

#define SDMA1_REG_OFFSET 0x600
#define SDMA2_REG_OFFSET 0x1cda0
#define SDMA3_REG_OFFSET 0x1d1a0
#define SDMA4_REG_OFFSET 0x1d5a0

/* helper function that allow only use sdma0 register offset
 * to calculate register offset for all the sdma instances */
static uint32_t sdma_v4_4_get_reg_offset(struct amdgpu_device *adev,
					 uint32_t instance,
					 uint32_t offset)
{
	uint32_t sdma_base = adev->reg_offset[SDMA0_HWIP][0][0];

	switch (instance) {
	case 0:
		return (sdma_base + offset);
	case 1:
		return (sdma_base + SDMA1_REG_OFFSET + offset);
	case 2:
		return (sdma_base + SDMA2_REG_OFFSET + offset);
	case 3:
		return (sdma_base + SDMA3_REG_OFFSET + offset);
	case 4:
		return (sdma_base + SDMA4_REG_OFFSET + offset);
	default:
		break;
	}
	return 0;
}

static const struct soc15_ras_field_entry sdma_v4_4_ras_fields[] = {
	{ "SDMA_MBANK_DATA_BUF0_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF0_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF1_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF1_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF2_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF2_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF3_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF3_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF4_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF4_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF5_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF5_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF6_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF6_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF7_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF7_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF8_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF8_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF9_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF9_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF10_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF10_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF11_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF11_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF12_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF12_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF13_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF13_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF14_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF14_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF15_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF15_SED),
	0, 0,
	},
	{ "SDMA_UCODE_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_UCODE_BUF_SED),
	0, 0,
	},
	{ "SDMA_RB_CMD_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_RB_CMD_BUF_SED),
	0, 0,
	},
	{ "SDMA_IB_CMD_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_IB_CMD_BUF_SED),
	0, 0,
	},
	{ "SDMA_UTCL1_RD_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_UTCL1_RD_FIFO_SED),
	0, 0,
	},
	{ "SDMA_UTCL1_RDBST_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_UTCL1_RDBST_FIFO_SED),
	0, 0,
	},
	{ "SDMA_DATA_LUT_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_DATA_LUT_FIFO_SED),
	0, 0,
	},
	{ "SDMA_SPLIT_DATA_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_SPLIT_DATA_BUF_SED),
	0, 0,
	},
	{ "SDMA_MC_WR_ADDR_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_MC_WR_ADDR_FIFO_SED),
	0, 0,
	},
	{ "SDMA_MC_RDRET_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_EDC_COUNTER2),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER2, SDMA_MC_WR_ADDR_FIFO_SED),
	0, 0,
	},
};

static void sdma_v4_4_get_ras_error_count(struct amdgpu_device *adev,
					  uint32_t value,
					  uint32_t instance,
					  uint32_t *sec_count)
{
	uint32_t i;
	uint32_t sec_cnt;

	/* double bits error (multiple bits) error detection is not supported */
	for (i = 0; i < ARRAY_SIZE(sdma_v4_4_ras_fields); i++) {
		/* the SDMA_EDC_COUNTER register in each sdma instance
		 * shares the same sed shift_mask
		 * */
		sec_cnt = (value &
			sdma_v4_4_ras_fields[i].sec_count_mask) >>
			sdma_v4_4_ras_fields[i].sec_count_shift;
		if (sec_cnt) {
			dev_info(adev->dev, "Detected %s in SDMA%d, SED %d\n",
				 sdma_v4_4_ras_fields[i].name,
				 instance, sec_cnt);
			*sec_count += sec_cnt;
		}
	}
}

static int sdma_v4_4_query_ras_error_count(struct amdgpu_device *adev,
					   uint32_t instance,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	uint32_t sec_count = 0;
	uint32_t reg_value = 0;
	uint32_t reg_offset = 0;

	reg_offset = sdma_v4_4_get_reg_offset(adev, instance, regSDMA0_EDC_COUNTER);
	reg_value = RREG32(reg_offset);
	/* double bit error is not supported */
	if (reg_value)
		sdma_v4_4_get_ras_error_count(adev, reg_value, instance, &sec_count);
	/* err_data->ce_count should be initialized to 0
	 * before calling into this function */
	err_data->ce_count += sec_count;
	/* double bit error is not supported
	 * set ue count to 0 */
	err_data->ue_count = 0;

	return 0;
};

static void sdma_v4_4_reset_ras_error_count(struct amdgpu_device *adev)
{
	int i;
	uint32_t reg_offset;

	/* write 0 to EDC_COUNTER reg to clear sdma edc counters */
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			reg_offset = sdma_v4_4_get_reg_offset(adev, i, regSDMA0_EDC_COUNTER);
			WREG32(reg_offset, 0);
			reg_offset = sdma_v4_4_get_reg_offset(adev, i, regSDMA0_EDC_COUNTER2);
			WREG32(reg_offset, 0);
		}
	}
}

const struct amdgpu_sdma_ras_funcs sdma_v4_4_ras_funcs = {
	.ras_late_init = amdgpu_sdma_ras_late_init,
	.ras_fini = amdgpu_sdma_ras_fini,
	.query_ras_error_count = sdma_v4_4_query_ras_error_count,
	.reset_ras_error_count = sdma_v4_4_reset_ras_error_count,
};
