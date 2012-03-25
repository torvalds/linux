#include <mach/system.h>

static void rk30_arch_reset(char mode, const char *cmd)
{
}

void (*arch_reset)(char, const char *) = rk30_arch_reset;
