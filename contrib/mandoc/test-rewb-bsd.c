#include <sys/types.h>
#include <stddef.h>
#include <regex.h>

int
main(void)
{
	regex_t	 re;

	if (regcomp(&re, "[[:<:]]word[[:>:]]", REG_EXTENDED | REG_NOSUB))
		return 1;
	if (regexec(&re, "the word is here", 0, NULL, 0))
		return 2;
	if (regexec(&re, "same word", 0, NULL, 0))
		return 3;
	if (regexec(&re, "word again", 0, NULL, 0))
		return 4;
	if (regexec(&re, "word", 0, NULL, 0))
		return 5;
	if (regexec(&re, "wordy", 0, NULL, 0) != REG_NOMATCH)
		return 6;
	if (regexec(&re, "sword", 0, NULL, 0) != REG_NOMATCH)
		return 7;
	if (regexec(&re, "reworded", 0, NULL, 0) != REG_NOMATCH)
		return 8;
	return 0;
}
