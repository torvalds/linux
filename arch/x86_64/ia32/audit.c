#include <asm-i386/unistd.h>

unsigned ia32_dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

unsigned ia32_chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};
