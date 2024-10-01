/* EXPORT_DATA_SYMBOL != EXPORT_SYMBOL here */
#define KSYM_FUNC(name) @fptr(name)
#include <asm-generic/export.h>
