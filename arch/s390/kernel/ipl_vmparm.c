// SPDX-License-Identifier: GPL-2.0
#include <asm/ebcdic.h>
#include <asm/ipl.h>

/* VM IPL PARM routines */
size_t ipl_block_get_ascii_vmparm(char *dest, size_t size,
				  const struct ipl_parameter_block *ipb)
{
	int i;
	size_t len;
	char has_lowercase = 0;

	len = 0;
	if ((ipb->ipl_info.ccw.vm_flags & DIAG308_VM_FLAGS_VP_VALID) &&
	    (ipb->ipl_info.ccw.vm_parm_len > 0)) {

		len = min_t(size_t, size - 1, ipb->ipl_info.ccw.vm_parm_len);
		memcpy(dest, ipb->ipl_info.ccw.vm_parm, len);
		/* If at least one character is lowercase, we assume mixed
		 * case; otherwise we convert everything to lowercase.
		 */
		for (i = 0; i < len; i++)
			if ((dest[i] > 0x80 && dest[i] < 0x8a) || /* a-i */
			    (dest[i] > 0x90 && dest[i] < 0x9a) || /* j-r */
			    (dest[i] > 0xa1 && dest[i] < 0xaa)) { /* s-z */
				has_lowercase = 1;
				break;
			}
		if (!has_lowercase)
			EBC_TOLOWER(dest, len);
		EBCASC(dest, len);
	}
	dest[len] = 0;

	return len;
}
