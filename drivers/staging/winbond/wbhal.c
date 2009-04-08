#include "sysdef.h"
#include "wbhal_f.h"
#include "wblinux_f.h"

unsigned char hal_get_dxx_reg(struct hw_data *pHwData, u16 number, u32 * pValue)
{
	if (number < 0x1000)
		number += 0x1000;
	return Wb35Reg_ReadSync(pHwData, number, pValue);
}

unsigned char hal_set_dxx_reg(struct hw_data *pHwData, u16 number, u32 value)
{
	unsigned char ret;

	if (number < 0x1000)
		number += 0x1000;
	ret = Wb35Reg_WriteSync(pHwData, number, value);
	return ret;
}
