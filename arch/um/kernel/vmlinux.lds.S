/* in case the preprocessor is a 32bit one */
#undef i386
#ifdef CONFIG_LD_SCRIPT_STATIC
#include "uml.lds.S"
#else
#include "dyn.lds.S"
#endif
