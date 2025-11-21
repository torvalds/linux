
/*
 * The symbols below are functions that are implemented by the startup code,
 * but called at runtime by the SEV code residing in the core kernel.
 */
PROVIDE(early_set_pages_state		= __pi_early_set_pages_state);
PROVIDE(early_snp_set_memory_private	= __pi_early_snp_set_memory_private);
PROVIDE(early_snp_set_memory_shared	= __pi_early_snp_set_memory_shared);
PROVIDE(get_hv_features			= __pi_get_hv_features);
PROVIDE(sev_es_terminate		= __pi_sev_es_terminate);
PROVIDE(snp_cpuid			= __pi_snp_cpuid);
PROVIDE(snp_cpuid_get_table		= __pi_snp_cpuid_get_table);
PROVIDE(svsm_issue_call			= __pi_svsm_issue_call);
PROVIDE(svsm_process_result_codes	= __pi_svsm_process_result_codes);
