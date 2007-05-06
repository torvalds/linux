struct stackframe {
	unsigned long fp;
	unsigned long sp;
	unsigned long lr;
	unsigned long pc;
};

int walk_stackframe(unsigned long fp, unsigned long low, unsigned long high,
		    int (*fn)(struct stackframe *, void *), void *data);
