#include <asm/mce.h>

enum severity_level {
	MCE_NO_SEVERITY,
	MCE_SOME_SEVERITY,
	MCE_UC_SEVERITY,
	MCE_PANIC_SEVERITY,
};

int mce_severity(struct mce *a, int tolerant, char **msg);
