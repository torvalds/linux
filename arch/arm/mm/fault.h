void do_bad_area(unsigned long addr, unsigned int fsr, struct pt_regs *regs);

unsigned long search_exception_table(unsigned long addr);
