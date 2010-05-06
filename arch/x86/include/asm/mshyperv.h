#ifndef ASM_X86__MSHYPER_H
#define ASM_X86__MSHYPER_H

int ms_hyperv_platform(void);
void __cpuinit ms_hyperv_set_feature_bits(struct cpuinfo_x86 *c);

#endif
