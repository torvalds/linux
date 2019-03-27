#define TE_PE
#define LEX_AT (LEX_BEGIN_NAME | LEX_NAME) /* Can have @'s inside labels.  */

/* The PE format supports long section names.  */
#define COFF_LONG_SECTION_NAMES

#include "obj-format.h"
