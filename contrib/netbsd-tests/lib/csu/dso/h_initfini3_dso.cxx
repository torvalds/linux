#include <unistd.h>

#ifdef CHECK_STACK_ALIGNMENT
#include <stdlib.h>

extern "C" int check_stack_alignment(void);
#endif

class Test2 {
public:
	Test2()
	{
		static const char msg[] = "constructor2 executed\n";
		write(STDOUT_FILENO, msg, sizeof(msg) - 1);
#ifdef CHECK_STACK_ALIGNMENT
		if (!check_stack_alignment()) {
			static const char msg2[] = "stack unaligned \n";
			write(STDOUT_FILENO, msg2, sizeof(msg2) - 1);
			exit(1);
		}
#endif
	}
	~Test2()
	{
		static const char msg[] = "destructor2 executed\n";
		write(STDOUT_FILENO, msg, sizeof(msg) - 1);
#ifdef CHECK_STACK_ALIGNMENT
		if (!check_stack_alignment()) {
			static const char msg2[] = "stack unaligned \n";
			write(STDOUT_FILENO, msg2, sizeof(msg2) - 1);
			exit(1);
		}
#endif
	}
};

Test2 test2;
