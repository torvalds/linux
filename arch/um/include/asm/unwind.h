#ifndef _ASM_UML_UNWIND_H
#define _ASM_UML_UNWIND_H

static inline void
unwind_module_init(struct module *mod, void *orc_ip, size_t orc_ip_size,
		   void *orc, size_t orc_size) {}

#endif /* _ASM_UML_UNWIND_H */
