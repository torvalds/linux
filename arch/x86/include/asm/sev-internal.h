/* SPDX-License-Identifier: GPL-2.0 */

#define DR7_RESET_VALUE        0x400

extern struct ghcb boot_ghcb_page;
extern u64 sev_hv_features;
extern u64 sev_secrets_pa;

/* #VC handler runtime per-CPU data */
struct sev_es_runtime_data {
	struct ghcb ghcb_page;

	/*
	 * Reserve one page per CPU as backup storage for the unencrypted GHCB.
	 * It is needed when an NMI happens while the #VC handler uses the real
	 * GHCB, and the NMI handler itself is causing another #VC exception. In
	 * that case the GHCB content of the first handler needs to be backed up
	 * and restored.
	 */
	struct ghcb backup_ghcb;

	/*
	 * Mark the per-cpu GHCBs as in-use to detect nested #VC exceptions.
	 * There is no need for it to be atomic, because nothing is written to
	 * the GHCB between the read and the write of ghcb_active. So it is safe
	 * to use it when a nested #VC exception happens before the write.
	 *
	 * This is necessary for example in the #VC->NMI->#VC case when the NMI
	 * happens while the first #VC handler uses the GHCB. When the NMI code
	 * raises a second #VC handler it might overwrite the contents of the
	 * GHCB written by the first handler. To avoid this the content of the
	 * GHCB is saved and restored when the GHCB is detected to be in use
	 * already.
	 */
	bool ghcb_active;
	bool backup_ghcb_active;

	/*
	 * Cached DR7 value - write it on DR7 writes and return it on reads.
	 * That value will never make it to the real hardware DR7 as debugging
	 * is currently unsupported in SEV-ES guests.
	 */
	unsigned long dr7;
};

struct ghcb_state {
	struct ghcb *ghcb;
};

extern struct svsm_ca boot_svsm_ca_page;

struct ghcb *__sev_get_ghcb(struct ghcb_state *state);
void __sev_put_ghcb(struct ghcb_state *state);

DECLARE_PER_CPU(struct sev_es_runtime_data*, runtime_data);
DECLARE_PER_CPU(struct sev_es_save_area *, sev_vmsa);

void early_set_pages_state(unsigned long vaddr, unsigned long paddr,
			   unsigned long npages, enum psc_op op);

DECLARE_PER_CPU(struct svsm_ca *, svsm_caa);
DECLARE_PER_CPU(u64, svsm_caa_pa);

extern struct svsm_ca *boot_svsm_caa;
extern u64 boot_svsm_caa_pa;

static __always_inline struct svsm_ca *svsm_get_caa(void)
{
	if (sev_cfg.use_cas)
		return this_cpu_read(svsm_caa);
	else
		return boot_svsm_caa;
}

static __always_inline u64 svsm_get_caa_pa(void)
{
	if (sev_cfg.use_cas)
		return this_cpu_read(svsm_caa_pa);
	else
		return boot_svsm_caa_pa;
}

int svsm_perform_call_protocol(struct svsm_call *call);

static inline u64 sev_es_rd_ghcb_msr(void)
{
	return native_rdmsrq(MSR_AMD64_SEV_ES_GHCB);
}

static __always_inline void sev_es_wr_ghcb_msr(u64 val)
{
	u32 low, high;

	low  = (u32)(val);
	high = (u32)(val >> 32);

	native_wrmsr(MSR_AMD64_SEV_ES_GHCB, low, high);
}

void snp_register_ghcb_early(unsigned long paddr);
bool sev_es_negotiate_protocol(void);
bool sev_es_check_cpu_features(void);
u64 get_hv_features(void);

const struct snp_cpuid_table *snp_cpuid_get_table(void);
