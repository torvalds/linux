#include <linux/config.h>
#ifdef CONFIG_LD_SCRIPT_STATIC
#include "uml.lds.S"
#else
#include "dyn.lds.S"
#endif
