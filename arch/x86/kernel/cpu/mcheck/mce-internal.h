#include <asm/mce.h>

enum severity_level {
	MCE_NO_SEVERITY,
	MCE_KEEP_SEVERITY,
	MCE_SOME_SEVERITY,
	MCE_AO_SEVERITY,
	MCE_UC_SEVERITY,
	MCE_AR_SEVERITY,
	MCE_PANIC_SEVERITY,
};

int mce_severity(struct mce *a, int tolerant, char **msg);

extern int mce_ser;
