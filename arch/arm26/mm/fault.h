void show_pte(struct mm_struct *mm, unsigned long addr);

int do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs);

unsigned long search_extable(unsigned long addr); //FIXME - is it right?
