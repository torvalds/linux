#include <linux/module.h>
#include <asm/ftrace.h>

#ifdef CONFIG_FUNCTION_TRACER
EXPORT_SYMBOL(_mcount);
#endif
