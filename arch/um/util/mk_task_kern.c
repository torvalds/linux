#include "linux/sched.h"
#include "linux/stddef.h"

extern void print(char *name, char *type, int offset);
extern void print_ptr(char *name, char *type, int offset);
extern void print_head(void);
extern void print_tail(void);

int main(int argc, char **argv)
{
  print_head();
  print_ptr("TASK_REGS", "union uml_pt_regs", 
	    offsetof(struct task_struct, thread.regs));
  print("TASK_PID", "int", offsetof(struct task_struct, pid));
  print_tail();
  return(0);
}
