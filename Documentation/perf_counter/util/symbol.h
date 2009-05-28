#ifndef _PERF_SYMBOL_
#define _PERF_SYMBOL_ 1

#include <linux/types.h>
#include "list.h"
#include "rbtree.h"

struct symbol {
	struct rb_node	rb_node;
	__u64		start;
	__u64		end;
	char		name[0];
};

struct dso {
	struct list_head node;
	struct rb_root	 syms;
	char		 name[0];
};

struct dso *dso__new(const char *name);
void dso__delete(struct dso *self);

struct symbol *dso__find_symbol(struct dso *self, uint64_t ip);

int dso__load_kallsyms(struct dso *self);
int dso__load_vmlinux(struct dso *self, const char *vmlinux);
int dso__load(struct dso *self);

size_t dso__fprintf(struct dso *self, FILE *fp);

void symbol__init(void);
#endif /* _PERF_SYMBOL_ */
