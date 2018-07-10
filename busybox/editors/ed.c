/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2002 by David I. Bell
 * Permission is granted to use, distribute, or modify this source,
 * provided that this copyright notice remains intact.
 *
 * The "ed" built-in command (much simplified)
 */
//config:config ED
//config:	bool "ed (25 kb)"
//config:	default y
//config:	help
//config:	The original 1970's Unix text editor, from the days of teletypes.
//config:	Small, simple, evil. Part of SUSv3. If you're not already using
//config:	this, you don't need it.

//kbuild:lib-$(CONFIG_ED) += ed.o

//applet:IF_ED(APPLET(ed, BB_DIR_BIN, BB_SUID_DROP))

//usage:#define ed_trivial_usage "[FILE]"
//usage:#define ed_full_usage ""

#include "libbb.h"
#include "common_bufsiz.h"

typedef struct LINE {
	struct LINE *next;
	struct LINE *prev;
	int len;
	char data[1];
} LINE;

#define searchString bb_common_bufsiz1

enum {
	USERSIZE = COMMON_BUFSIZE > 1024 ? 1024
	         : COMMON_BUFSIZE - 1, /* max line length typed in by user */
	INITBUF_SIZE = 1024, /* initial buffer size */
};

struct globals {
	int curNum;
	int lastNum;
	int bufUsed;
	int bufSize;
	LINE *curLine;
	char *bufBase;
	char *bufPtr;
	char *fileName;
	LINE lines;
	smallint dirty;
	int marks[26];
};
#define G (*ptr_to_globals)
#define curLine            (G.curLine           )
#define bufBase            (G.bufBase           )
#define bufPtr             (G.bufPtr            )
#define fileName           (G.fileName          )
#define curNum             (G.curNum            )
#define lastNum            (G.lastNum           )
#define bufUsed            (G.bufUsed           )
#define bufSize            (G.bufSize           )
#define dirty              (G.dirty             )
#define lines              (G.lines             )
#define marks              (G.marks             )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

static int bad_nums(int num1, int num2, const char *for_what)
{
	if ((num1 < 1) || (num2 > lastNum) || (num1 > num2)) {
		bb_error_msg("bad line range for %s", for_what);
		return 1;
	}
	return 0;
}

/*
 * Return a pointer to the specified line number.
 */
static LINE *findLine(int num)
{
	LINE *lp;
	int lnum;

	if ((num < 1) || (num > lastNum)) {
		bb_error_msg("line number %d does not exist", num);
		return NULL;
	}

	if (curNum <= 0) {
		curNum = 1;
		curLine = lines.next;
	}

	if (num == curNum)
		return curLine;

	lp = curLine;
	lnum = curNum;
	if (num < (curNum / 2)) {
		lp = lines.next;
		lnum = 1;
	} else if (num > ((curNum + lastNum) / 2)) {
		lp = lines.prev;
		lnum = lastNum;
	}

	while (lnum < num) {
		lp = lp->next;
		lnum++;
	}

	while (lnum > num) {
		lp = lp->prev;
		lnum--;
	}
	return lp;
}

/*
 * Search a line for the specified string starting at the specified
 * offset in the line.  Returns the offset of the found string, or -1.
 */
static int findString(const LINE *lp, const char *str, int len, int offset)
{
	int left;
	const char *cp, *ncp;

	cp = &lp->data[offset];
	left = lp->len - offset - len;

	while (left >= 0) {
		ncp = memchr(cp, str[0], left + 1);
		if (ncp == NULL)
			return -1;
		left -= (ncp - cp);
		cp = ncp;
		if (memcmp(cp, str, len) == 0)
			return (cp - lp->data);
		cp++;
		left--;
	}

	return -1;
}

/*
 * Search for a line which contains the specified string.
 * If the string is "", then the previously searched for string
 * is used.  The currently searched for string is saved for future use.
 * Returns the line number which matches, or 0 if there was no match
 * with an error printed.
 */
static NOINLINE int searchLines(const char *str, int num1, int num2)
{
	const LINE *lp;
	int len;

	if (bad_nums(num1, num2, "search"))
		return 0;

	if (*str == '\0') {
		if (searchString[0] == '\0') {
			bb_error_msg("no previous search string");
			return 0;
		}
		str = searchString;
	}

	if (str != searchString)
		strcpy(searchString, str);

	len = strlen(str);

	lp = findLine(num1);
	if (lp == NULL)
		return 0;

	while (num1 <= num2) {
		if (findString(lp, str, len, 0) >= 0)
			return num1;
		num1++;
		lp = lp->next;
	}

	bb_error_msg("can't find string \"%s\"", str);
	return 0;
}

/*
 * Parse a line number argument if it is present.  This is a sum
 * or difference of numbers, ".", "$", "'c", or a search string.
 * Returns pointer which stopped the scan if successful
 * (whether or not there was a number).
 * Returns NULL if there was a parsing error, with a message output.
 * Whether there was a number is returned indirectly, as is the number.
 */
static const char* getNum(const char *cp, smallint *retHaveNum, int *retNum)
{
	char *endStr, str[USERSIZE];
	int value, num;
	smallint haveNum, minus;

	value = 0;
	haveNum = FALSE;
	minus = 0;

	while (TRUE) {
		cp = skip_whitespace(cp);

		switch (*cp) {
			case '.':
				haveNum = TRUE;
				num = curNum;
				cp++;
				break;

			case '$':
				haveNum = TRUE;
				num = lastNum;
				cp++;
				break;

			case '\'':
				cp++;
				if ((unsigned)(*cp - 'a') >= 26) {
					bb_error_msg("bad mark name");
					return NULL;
				}
				haveNum = TRUE;
				num = marks[(unsigned)(*cp - 'a')];
				cp++;
				break;

			case '/':
				strcpy(str, ++cp);
				endStr = strchr(str, '/');
				if (endStr) {
					*endStr++ = '\0';
					cp += (endStr - str);
				} else
					cp = "";
				num = searchLines(str, curNum, lastNum);
				if (num == 0)
					return NULL;
				haveNum = TRUE;
				break;

			default:
				if (!isdigit(*cp)) {
					*retHaveNum = haveNum;
					*retNum = value;
					return cp;
				}
				num = 0;
				while (isdigit(*cp))
					num = num * 10 + *cp++ - '0';
				haveNum = TRUE;
				break;
		}

		value += (minus ? -num : num);

		cp = skip_whitespace(cp);

		switch (*cp) {
			case '-':
				minus = 1;
				cp++;
				break;

			case '+':
				minus = 0;
				cp++;
				break;

			default:
				*retHaveNum = haveNum;
				*retNum = value;
				return cp;
		}
	}
}

/*
 * Set the current line number.
 * Returns TRUE if successful.
 */
static int setCurNum(int num)
{
	LINE *lp;

	lp = findLine(num);
	if (lp == NULL)
		return FALSE;
	curNum = num;
	curLine = lp;
	return TRUE;
}

/*
 * Insert a new line with the specified text.
 * The line is inserted so as to become the specified line,
 * thus pushing any existing and further lines down one.
 * The inserted line is also set to become the current line.
 * Returns TRUE if successful.
 */
static int insertLine(int num, const char *data, int len)
{
	LINE *newLp, *lp;

	if ((num < 1) || (num > lastNum + 1)) {
		bb_error_msg("inserting at bad line number");
		return FALSE;
	}

	newLp = xmalloc(sizeof(LINE) + len - 1);

	memcpy(newLp->data, data, len);
	newLp->len = len;

	if (num > lastNum)
		lp = &lines;
	else {
		lp = findLine(num);
		if (lp == NULL) {
			free((char *) newLp);
			return FALSE;
		}
	}

	newLp->next = lp;
	newLp->prev = lp->prev;
	lp->prev->next = newLp;
	lp->prev = newLp;

	lastNum++;
	dirty = TRUE;
	return setCurNum(num);
}

/*
 * Add lines which are typed in by the user.
 * The lines are inserted just before the specified line number.
 * The lines are terminated by a line containing a single dot (ugly!),
 * or by an end of file.
 */
static void addLines(int num)
{
	int len;
	char buf[USERSIZE + 1];

	while (1) {
		/* Returns:
		 * -1 on read errors or EOF, or on bare Ctrl-D.
		 * 0  on ctrl-C,
		 * >0 length of input string, including terminating '\n'
		 */
		len = read_line_input(NULL, "", buf, sizeof(buf));
		if (len <= 0) {
			/* Previously, ctrl-C was exiting to shell.
			 * Now we exit to ed prompt. Is in important? */
			return;
		}
		if (buf[0] == '.' && buf[1] == '\n' && buf[2] == '\0')
			return;
		if (!insertLine(num++, buf, len))
			return;
	}
}

/*
 * Read lines from a file at the specified line number.
 * Returns TRUE if the file was successfully read.
 */
static int readLines(const char *file, int num)
{
	int fd, cc;
	int len, lineCount, charCount;
	char *cp;

	if ((num < 1) || (num > lastNum + 1)) {
		bb_error_msg("bad line for read");
		return FALSE;
	}

	fd = open(file, 0);
	if (fd < 0) {
		bb_simple_perror_msg(file);
		return FALSE;
	}

	bufPtr = bufBase;
	bufUsed = 0;
	lineCount = 0;
	charCount = 0;
	cc = 0;

	printf("\"%s\", ", file);
	fflush_all();

	do {
		cp = memchr(bufPtr, '\n', bufUsed);

		if (cp) {
			len = (cp - bufPtr) + 1;
			if (!insertLine(num, bufPtr, len)) {
				close(fd);
				return FALSE;
			}
			bufPtr += len;
			bufUsed -= len;
			charCount += len;
			lineCount++;
			num++;
			continue;
		}

		if (bufPtr != bufBase) {
			memcpy(bufBase, bufPtr, bufUsed);
			bufPtr = bufBase + bufUsed;
		}

		if (bufUsed >= bufSize) {
			len = (bufSize * 3) / 2;
			cp = xrealloc(bufBase, len);
			bufBase = cp;
			bufPtr = bufBase + bufUsed;
			bufSize = len;
		}

		cc = safe_read(fd, bufPtr, bufSize - bufUsed);
		bufUsed += cc;
		bufPtr = bufBase;
	} while (cc > 0);

	if (cc < 0) {
		bb_simple_perror_msg(file);
		close(fd);
		return FALSE;
	}

	if (bufUsed) {
		if (!insertLine(num, bufPtr, bufUsed)) {
			close(fd);
			return -1;
		}
		lineCount++;
		charCount += bufUsed;
	}

	close(fd);

	printf("%d lines%s, %d chars\n", lineCount,
		(bufUsed ? " (incomplete)" : ""), charCount);

	return TRUE;
}

/*
 * Write the specified lines out to the specified file.
 * Returns TRUE if successful, or FALSE on an error with a message output.
 */
static int writeLines(const char *file, int num1, int num2)
{
	LINE *lp;
	int fd, lineCount, charCount;

	if (bad_nums(num1, num2, "write"))
		return FALSE;

	lineCount = 0;
	charCount = 0;

	fd = creat(file, 0666);
	if (fd < 0) {
		bb_simple_perror_msg(file);
		return FALSE;
	}

	printf("\"%s\", ", file);
	fflush_all();

	lp = findLine(num1);
	if (lp == NULL) {
		close(fd);
		return FALSE;
	}

	while (num1++ <= num2) {
		if (full_write(fd, lp->data, lp->len) != lp->len) {
			bb_simple_perror_msg(file);
			close(fd);
			return FALSE;
		}
		charCount += lp->len;
		lineCount++;
		lp = lp->next;
	}

	if (close(fd) < 0) {
		bb_simple_perror_msg(file);
		return FALSE;
	}

	printf("%d lines, %d chars\n", lineCount, charCount);
	return TRUE;
}

/*
 * Print lines in a specified range.
 * The last line printed becomes the current line.
 * If expandFlag is TRUE, then the line is printed specially to
 * show magic characters.
 */
static int printLines(int num1, int num2, int expandFlag)
{
	const LINE *lp;
	const char *cp;
	int ch, count;

	if (bad_nums(num1, num2, "print"))
		return FALSE;

	lp = findLine(num1);
	if (lp == NULL)
		return FALSE;

	while (num1 <= num2) {
		if (!expandFlag) {
			write(STDOUT_FILENO, lp->data, lp->len);
			setCurNum(num1++);
			lp = lp->next;
			continue;
		}

		/*
		 * Show control characters and characters with the
		 * high bit set specially.
		 */
		cp = lp->data;
		count = lp->len;

		if ((count > 0) && (cp[count - 1] == '\n'))
			count--;

		while (count-- > 0) {
			ch = (unsigned char) *cp++;
			fputc_printable(ch | PRINTABLE_META, stdout);
		}

		fputs("$\n", stdout);

		setCurNum(num1++);
		lp = lp->next;
	}

	return TRUE;
}

/*
 * Delete lines from the given range.
 */
static void deleteLines(int num1, int num2)
{
	LINE *lp, *nlp, *plp;
	int count;

	if (bad_nums(num1, num2, "delete"))
		return;

	lp = findLine(num1);
	if (lp == NULL)
		return;

	if ((curNum >= num1) && (curNum <= num2)) {
		if (num2 < lastNum)
			setCurNum(num2 + 1);
		else if (num1 > 1)
			setCurNum(num1 - 1);
		else
			curNum = 0;
	}

	count = num2 - num1 + 1;
	if (curNum > num2)
		curNum -= count;
	lastNum -= count;

	while (count-- > 0) {
		nlp = lp->next;
		plp = lp->prev;
		plp->next = nlp;
		nlp->prev = plp;
		free(lp);
		lp = nlp;
	}

	dirty = TRUE;
}

/*
 * Do the substitute command.
 * The current line is set to the last substitution done.
 */
static void subCommand(const char *cmd, int num1, int num2)
{
	char *cp, *oldStr, *newStr, buf[USERSIZE];
	int delim, oldLen, newLen, deltaLen, offset;
	LINE *lp, *nlp;
	int globalFlag, printFlag, didSub, needPrint;

	if (bad_nums(num1, num2, "substitute"))
		return;

	globalFlag = FALSE;
	printFlag = FALSE;
	didSub = FALSE;
	needPrint = FALSE;

	/*
	 * Copy the command so we can modify it.
	 */
	strcpy(buf, cmd);
	cp = buf;

	if (isblank(*cp) || (*cp == '\0')) {
		bb_error_msg("bad delimiter for substitute");
		return;
	}

	delim = *cp++;
	oldStr = cp;

	cp = strchr(cp, delim);
	if (cp == NULL) {
		bb_error_msg("missing 2nd delimiter for substitute");
		return;
	}

	*cp++ = '\0';

	newStr = cp;
	cp = strchr(cp, delim);

	if (cp)
		*cp++ = '\0';
	else
		cp = (char*)"";

	while (*cp) switch (*cp++) {
		case 'g':
			globalFlag = TRUE;
			break;
		case 'p':
			printFlag = TRUE;
			break;
		default:
			bb_error_msg("unknown option for substitute");
			return;
	}

	if (*oldStr == '\0') {
		if (searchString[0] == '\0') {
			bb_error_msg("no previous search string");
			return;
		}
		oldStr = searchString;
	}

	if (oldStr != searchString)
		strcpy(searchString, oldStr);

	lp = findLine(num1);
	if (lp == NULL)
		return;

	oldLen = strlen(oldStr);
	newLen = strlen(newStr);
	deltaLen = newLen - oldLen;
	offset = 0;
	nlp = NULL;

	while (num1 <= num2) {
		offset = findString(lp, oldStr, oldLen, offset);

		if (offset < 0) {
			if (needPrint) {
				printLines(num1, num1, FALSE);
				needPrint = FALSE;
			}
			offset = 0;
			lp = lp->next;
			num1++;
			continue;
		}

		needPrint = printFlag;
		didSub = TRUE;
		dirty = TRUE;

		/*
		 * If the replacement string is the same size or shorter
		 * than the old string, then the substitution is easy.
		 */
		if (deltaLen <= 0) {
			memcpy(&lp->data[offset], newStr, newLen);
			if (deltaLen) {
				memcpy(&lp->data[offset + newLen],
					&lp->data[offset + oldLen],
					lp->len - offset - oldLen);

				lp->len += deltaLen;
			}
			offset += newLen;
			if (globalFlag)
				continue;
			if (needPrint) {
				printLines(num1, num1, FALSE);
				needPrint = FALSE;
			}
			lp = lp->next;
			num1++;
			continue;
		}

		/*
		 * The new string is larger, so allocate a new line
		 * structure and use that.  Link it in place of
		 * the old line structure.
		 */
		nlp = xmalloc(sizeof(LINE) + lp->len + deltaLen);

		nlp->len = lp->len + deltaLen;

		memcpy(nlp->data, lp->data, offset);
		memcpy(&nlp->data[offset], newStr, newLen);
		memcpy(&nlp->data[offset + newLen],
			&lp->data[offset + oldLen],
			lp->len - offset - oldLen);

		nlp->next = lp->next;
		nlp->prev = lp->prev;
		nlp->prev->next = nlp;
		nlp->next->prev = nlp;

		if (curLine == lp)
			curLine = nlp;

		free(lp);
		lp = nlp;

		offset += newLen;

		if (globalFlag)
			continue;

		if (needPrint) {
			printLines(num1, num1, FALSE);
			needPrint = FALSE;
		}

		lp = lp->next;
		num1++;
	}

	if (!didSub)
		bb_error_msg("no substitutions found for \"%s\"", oldStr);
}

/*
 * Read commands until we are told to stop.
 */
static void doCommands(void)
{
	while (TRUE) {
		char buf[USERSIZE];
		const char *cp;
		int len;
		int n, num1, num2;
		smallint h, have1, have2;

		/* Returns:
		 * -1 on read errors or EOF, or on bare Ctrl-D.
		 * 0  on ctrl-C,
		 * >0 length of input string, including terminating '\n'
		 */
		len = read_line_input(NULL, ": ", buf, sizeof(buf));
		if (len <= 0)
			return;
		while (len && isspace(buf[--len]))
			buf[len] = '\0';

		if ((curNum == 0) && (lastNum > 0)) {
			curNum = 1;
			curLine = lines.next;
		}

		have1 = FALSE;
		have2 = FALSE;
		/* Don't pass &haveN, &numN to getNum() since this forces
		 * compiler to keep them on stack, not in registers,
		 * which is usually quite suboptimal.
		 * Using intermediate variables shrinks code by ~150 bytes.
		 */
		cp = getNum(skip_whitespace(buf), &h, &n);
		if (!cp)
			continue;
		have1 = h;
		num1 = n;
		cp = skip_whitespace(cp);
		if (*cp == ',') {
			cp = getNum(cp + 1, &h, &n);
			if (!cp)
				continue;
			num2 = n;
			if (!have1)
				num1 = 1;
			if (!h)
				num2 = lastNum;
			have1 = TRUE;
			have2 = TRUE;
		}
		if (!have1)
			num1 = curNum;
		if (!have2)
			num2 = num1;

		switch (*cp++) {
		case 'a':
			addLines(num1 + 1);
			break;

		case 'c':
			deleteLines(num1, num2);
			addLines(num1);
			break;

		case 'd':
			deleteLines(num1, num2);
			break;

		case 'f':
			if (*cp != '\0' && *cp != ' ') {
				bb_error_msg("bad file command");
				break;
			}
			cp = skip_whitespace(cp);
			if (*cp == '\0') {
				if (fileName)
					printf("\"%s\"\n", fileName);
				else
					puts("No file name");
				break;
			}
			free(fileName);
			fileName = xstrdup(cp);
			break;

		case 'i':
			if (!have1 && lastNum == 0)
				num1 = 1;
			addLines(num1);
			break;

		case 'k':
			cp = skip_whitespace(cp);
			if ((unsigned)(*cp - 'a') >= 26 || cp[1]) {
				bb_error_msg("bad mark name");
				break;
			}
			marks[(unsigned)(*cp - 'a')] = num2;
			break;

		case 'l':
			printLines(num1, num2, TRUE);
			break;

		case 'p':
			printLines(num1, num2, FALSE);
			break;

		case 'q':
			cp = skip_whitespace(cp);
			if (have1 || *cp) {
				bb_error_msg("bad quit command");
				break;
			}
			if (!dirty)
				return;
			len = read_line_input(NULL, "Really quit? ", buf, 16);
			/* read error/EOF - no way to continue */
			if (len < 0)
				return;
			cp = skip_whitespace(buf);
			if ((*cp | 0x20) == 'y') /* Y or y */
				return;
			break;

		case 'r':
			if (*cp != '\0' && *cp != ' ') {
				bb_error_msg("bad read command");
				break;
			}
			cp = skip_whitespace(cp);
			if (*cp == '\0') {
				bb_error_msg("no file name");
				break;
			}
			if (!have1)
				num1 = lastNum;
			if (readLines(cp, num1 + 1))
				break;
			if (fileName == NULL)
				fileName = xstrdup(cp);
			break;

		case 's':
			subCommand(cp, num1, num2);
			break;

		case 'w':
			if (*cp != '\0' && *cp != ' ') {
				bb_error_msg("bad write command");
				break;
			}
			cp = skip_whitespace(cp);
			if (*cp == '\0') {
				cp = fileName;
				if (!cp) {
					bb_error_msg("no file name specified");
					break;
				}
			}
			if (!have1) {
				num1 = 1;
				num2 = lastNum;
				dirty = FALSE;
			}
			writeLines(cp, num1, num2);
			break;

		case 'z':
			switch (*cp) {
			case '-':
				printLines(curNum - 21, curNum, FALSE);
				break;
			case '.':
				printLines(curNum - 11, curNum + 10, FALSE);
				break;
			default:
				printLines(curNum, curNum + 21, FALSE);
				break;
			}
			break;

		case '.':
			if (have1) {
				bb_error_msg("no arguments allowed");
				break;
			}
			printLines(curNum, curNum, FALSE);
			break;

		case '-':
			if (setCurNum(curNum - 1))
				printLines(curNum, curNum, FALSE);
			break;

		case '=':
			printf("%d\n", num1);
			break;
		case '\0':
			if (have1) {
				printLines(num2, num2, FALSE);
				break;
			}
			if (setCurNum(curNum + 1))
				printLines(curNum, curNum, FALSE);
			break;

		default:
			bb_error_msg("unimplemented command");
			break;
		}
	}
}

int ed_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ed_main(int argc UNUSED_PARAM, char **argv)
{
	INIT_G();

	bufSize = INITBUF_SIZE;
	bufBase = xmalloc(bufSize);
	bufPtr = bufBase;
	lines.next = &lines;
	lines.prev = &lines;

	if (argv[1]) {
		fileName = xstrdup(argv[1]);
		if (!readLines(fileName, 1)) {
			return EXIT_SUCCESS;
		}
		if (lastNum)
			setCurNum(1);
		dirty = FALSE;
	}

	doCommands();
	return EXIT_SUCCESS;
}
