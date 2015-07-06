#ifndef _ASM_X86_INTEL_PMC_IPC_H_
#define  _ASM_X86_INTEL_PMC_IPC_H_

/* Commands */
#define PMC_IPC_PMIC_ACCESS		0xFF
#define		PMC_IPC_PMIC_ACCESS_READ	0x0
#define		PMC_IPC_PMIC_ACCESS_WRITE	0x1
#define PMC_IPC_USB_PWR_CTRL		0xF0
#define PMC_IPC_PMIC_BLACKLIST_SEL	0xEF
#define PMC_IPC_PHY_CONFIG		0xEE
#define PMC_IPC_NORTHPEAK_CTRL		0xED
#define PMC_IPC_PM_DEBUG		0xEC
#define PMC_IPC_PMC_TELEMTRY		0xEB
#define PMC_IPC_PMC_FW_MSG_CTRL		0xEA

/* IPC return code */
#define IPC_ERR_NONE			0
#define IPC_ERR_CMD_NOT_SUPPORTED	1
#define IPC_ERR_CMD_NOT_SERVICED	2
#define IPC_ERR_UNABLE_TO_SERVICE	3
#define IPC_ERR_CMD_INVALID		4
#define IPC_ERR_CMD_FAILED		5
#define IPC_ERR_EMSECURITY		6
#define IPC_ERR_UNSIGNEDKERNEL		7

#if IS_ENABLED(CONFIG_INTEL_PMC_IPC)

/*
 * intel_pmc_ipc_simple_command
 * @cmd: command
 * @sub: sub type
 */
int intel_pmc_ipc_simple_command(int cmd, int sub);

/*
 * intel_pmc_ipc_raw_cmd
 * @cmd: command
 * @sub: sub type
 * @in: input data
 * @inlen: input length in bytes
 * @out: output data
 * @outlen: output length in dwords
 * @sptr: data writing to SPTR register
 * @dptr: data writing to DPTR register
 */
int intel_pmc_ipc_raw_cmd(u32 cmd, u32 sub, u8 *in, u32 inlen,
		u32 *out, u32 outlen, u32 dptr, u32 sptr);

/*
 * intel_pmc_ipc_command
 * @cmd: command
 * @sub: sub type
 * @in: input data
 * @inlen: input length in bytes
 * @out: output data
 * @outlen: output length in dwords
 */
int intel_pmc_ipc_command(u32 cmd, u32 sub, u8 *in, u32 inlen,
		u32 *out, u32 outlen);

#else

static inline int intel_pmc_ipc_simple_command(int cmd, int sub)
{
	return -EINVAL;
}

static inline int intel_pmc_ipc_raw_cmd(u32 cmd, u32 sub, u8 *in, u32 inlen,
		u32 *out, u32 outlen, u32 dptr, u32 sptr)
{
	return -EINVAL;
}

static inline int intel_pmc_ipc_command(u32 cmd, u32 sub, u8 *in, u32 inlen,
		u32 *out, u32 outlen)
{
	return -EINVAL;
}

#endif /*CONFIG_INTEL_PMC_IPC*/

#endif
