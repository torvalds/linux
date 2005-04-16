void do_bad_area(struct task_struct *tsk, struct mm_struct *mm,
		 unsigned long addr, unsigned int fsr, struct pt_regs *regs);

void show_pte(struct mm_struct *mm, unsigned long addr);

unsigned long search_exception_table(unsigned long addr);
