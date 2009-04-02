#include "linux/sched.h"
#include "asm/ptrace.h"

int putreg(struct task_struct *child, unsigned long regno, 
		  unsigned long value)
{
	child->thread.process_regs.regs[regno >> 2] = value;
	return 0;
}

int poke_user(struct task_struct *child, long addr, long data)
{
	if ((addr & 3) || addr < 0)
		return -EIO;

	if (addr < MAX_REG_OFFSET)
		return putreg(child, addr, data);

	else if((addr >= offsetof(struct user, u_debugreg[0])) &&
		(addr <= offsetof(struct user, u_debugreg[7]))){
		  addr -= offsetof(struct user, u_debugreg[0]);
		  addr = addr >> 2;
		  if((addr == 4) || (addr == 5)) return -EIO;
		  child->thread.arch.debugregs[addr] = data;
		  return 0;
	}
	return -EIO;
}

unsigned long getreg(struct task_struct *child, unsigned long regno)
{
	unsigned long retval = ~0UL;

	retval &= child->thread.process_regs.regs[regno >> 2];
	return retval;
}

int peek_user(struct task_struct *child, long addr, long data)
{
	/* read the word at location addr in the USER area. */
	unsigned long tmp;

	if ((addr & 3) || addr < 0)
		return -EIO;

	tmp = 0;  /* Default return condition */
	if(addr < MAX_REG_OFFSET){
		tmp = getreg(child, addr);
	}
	else if((addr >= offsetof(struct user, u_debugreg[0])) &&
		(addr <= offsetof(struct user, u_debugreg[7]))){
		addr -= offsetof(struct user, u_debugreg[0]);
		addr = addr >> 2;
		tmp = child->thread.arch.debugregs[addr];
	}
	return put_user(tmp, (unsigned long *) data);
}

