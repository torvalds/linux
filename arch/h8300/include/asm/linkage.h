#ifndef _H8300_LINKAGE_H
#define _H8300_LINKAGE_H

#undef SYMBOL_NAME_LABEL
#define SYMBOL_NAME_LABEL(_name_) _##_name_##:
#endif
