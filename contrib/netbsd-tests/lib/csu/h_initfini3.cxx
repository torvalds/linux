#include <dlfcn.h>
#include <err.h>
#include <unistd.h>

int
main(void)
{
	static const char msg1[] = "main started\n";
	static const char msg2[] = "main after dlopen\n";
	static const char msg3[] = "main terminated\n";

	void *handle;

	write(STDOUT_FILENO, msg1, sizeof(msg1) - 1);
	handle = dlopen("h_initfini3_dso.so", RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL)
		err(1, "dlopen");
	write(STDOUT_FILENO, msg2, sizeof(msg2) - 1);
	dlclose(handle);
	write(STDOUT_FILENO, msg3, sizeof(msg3) - 1);
	return 0;
}
