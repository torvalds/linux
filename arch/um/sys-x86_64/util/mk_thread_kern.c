#include "linux/config.h"
#include "linux/stddef.h"
#include "linux/sched.h"

extern void print_head(void);
extern void print_constant_ptr(char *name, int value);
extern void print_constant(char *name, char *type, int value);
extern void print_tail(void);

#define THREAD_OFFSET(field) offsetof(struct task_struct, thread.field)

int main(int argc, char **argv)
{
  print_head();
#ifdef CONFIG_MODE_TT
  print_constant("TASK_EXTERN_PID", "int", THREAD_OFFSET(mode.tt.extern_pid));
#endif
  print_tail();
  return(0);
}

