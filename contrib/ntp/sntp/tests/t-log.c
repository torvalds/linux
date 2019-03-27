#include "config.h"
#include "unity.h"
#include "ntp_types.h"


//#include "log.h"
#include "log.c"

void setUp(void);
void testChangePrognameInMysyslog(void);
void testOpenLogfileTest(void);
void testWriteInCustomLogfile(void);


void
setUp(void) {
	init_lib();
}


//in var/log/syslog (may differ depending on your OS), logged name of the program will be "TEST_PROGNAME".

void
testChangePrognameInMysyslog(void)
{
	sntp_init_logging("TEST_PROGNAME");
	msyslog(LOG_ERR, "TESTING sntp_init_logging()");

	return;
}

//writes log files in your own file instead of syslog! (MAY BE USEFUL TO SUPPRESS ERROR MESSAGES!)

void
testOpenLogfileTest(void)
{
	sntp_init_logging("TEST_PROGNAME2"); //this name is consistent through the entire program unless changed
	open_logfile("testLogfile.log");
	//open_logfile("/var/log/syslog"); //this gives me "Permission Denied" when i do %m

	msyslog(LOG_ERR, "Cannot open log file %s","abcXX");
	//cleanup_log(); //unnecessary  after log.c fix!

	return;
}


//multiple cleanup_log() causes segfault. Probably the reason it's static. Opening multiple open_logfile(name) will cause segfault x.x I'm guessing it's not intended to be changed. Cleanup after unity test doesn't fix it, looks like. Calling in tearDown() also causes issues.

void
testWriteInCustomLogfile(void)
{
	char testString[256] = "12345 ABC";
	char testName[256] = "TEST_PROGNAME3";

	(void)remove("testLogfile2.log");

	sntp_init_logging(testName);
	open_logfile("testLogfile2.log"); // ./ causing issues
	//sntp_init_logging(testName);


	msyslog(LOG_ERR, "%s", testString);
	FILE * f = fopen("testLogfile2.log","r");
	char line[256];

	TEST_ASSERT_TRUE( f != NULL);

	//should be only 1 line
	while (fgets(line, sizeof(line), f)) {
		printf("%s", line);
	}


	char* x = strstr(line,testName);

	TEST_ASSERT_TRUE( x != NULL);

	x = strstr(line,testString);
	TEST_ASSERT_TRUE( x != NULL);
	//cleanup_log();
	fclose(f); //using this will also cause segfault, because at the end, log.c will  call (using atexit(func) function) cleanup_log(void)-> fclose(syslog_file);
	//After the 1st fclose, syslog_file = NULL, and is never reset -> hopefully fixed by editing log.c
	//TEST_ASSERT_EQUAL_STRING(testString,line); //doesn't work, line is dynamic because the process name is random.

	return;
}
