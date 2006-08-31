#undef __s390x__
#include <asm/unistd.h>

unsigned s390_dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

unsigned s390_chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};

unsigned s390_write_class[] = {
#include <asm-generic/audit_write.h>
~0U
};

unsigned s390_read_class[] = {
#include <asm-generic/audit_read.h>
~0U
};
