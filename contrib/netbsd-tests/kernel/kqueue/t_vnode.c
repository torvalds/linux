#include <sys/event.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

/*
 * Test cases for events triggered by manipulating a target directory
 * content.  Using EVFILT_VNODE filter on the target directory descriptor.
 *
 */

static const char *dir_target = "foo";
static const char *dir_inside1 = "foo/bar1";
static const char *dir_inside2 = "foo/bar2";
static const char *dir_outside = "bar";
static const char *file_inside1 = "foo/baz1";
static const char *file_inside2 = "foo/baz2";
static const char *file_outside = "qux";
static const struct timespec ts = {0, 0};
static int kq = -1;
static int target = -1;

int init_target(void);
int init_kqueue(void);
int create_file(const char *);
void cleanup(void);

int
init_target(void)
{
	if (mkdir(dir_target, S_IRWXU) < 0) {
		return -1;
	}
	target = open(dir_target, O_RDONLY, 0);
	return target;
}

int
init_kqueue(void)
{
	struct kevent eventlist[1];

	kq = kqueue();
	if (kq < 0) {
		return -1;
	}
	EV_SET(&eventlist[0], (uintptr_t)target, EVFILT_VNODE,
		EV_ADD | EV_ONESHOT, NOTE_DELETE |
		NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB |
		NOTE_LINK | NOTE_RENAME | NOTE_REVOKE, 0, 0);
	return kevent(kq, eventlist, 1, NULL, 0, NULL);
}

int
create_file(const char *file)
{
	int fd;

	fd = open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		return -1;
	}
	return close(fd);
}

void
cleanup(void)
{
	(void)unlink(file_inside1);
	(void)unlink(file_inside2);
	(void)unlink(file_outside);
	(void)rmdir(dir_inside1);
	(void)rmdir(dir_inside2);
	(void)rmdir(dir_outside);
	(void)rmdir(dir_target);
	(void)close(kq);
	(void)close(target);
}

ATF_TC_WITH_CLEANUP(dir_no_note_link_create_file_in);
ATF_TC_HEAD(dir_no_note_link_create_file_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) does not return NOTE_LINK for the directory "
		"'foo' if a file 'foo/baz' is created.");
}
ATF_TC_BODY(dir_no_note_link_create_file_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, 0);
}
ATF_TC_CLEANUP(dir_no_note_link_create_file_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_no_note_link_delete_file_in);
ATF_TC_HEAD(dir_no_note_link_delete_file_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) does not return NOTE_LINK for the directory "
		"'foo' if a file 'foo/baz' is deleted.");
}
ATF_TC_BODY(dir_no_note_link_delete_file_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(unlink(file_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, 0);
}
ATF_TC_CLEANUP(dir_no_note_link_delete_file_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_no_note_link_mv_dir_within);
ATF_TC_HEAD(dir_no_note_link_mv_dir_within, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) does not return NOTE_LINK for the directory "
		"'foo' if a directory 'foo/bar' is renamed to 'foo/baz'.");
}
ATF_TC_BODY(dir_no_note_link_mv_dir_within, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_inside1, dir_inside2) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, 0);
}
ATF_TC_CLEANUP(dir_no_note_link_mv_dir_within, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_no_note_link_mv_file_within);
ATF_TC_HEAD(dir_no_note_link_mv_file_within, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) does not return NOTE_LINK for the directory "
		"'foo' if a file 'foo/baz' is renamed to 'foo/qux'.");
}
ATF_TC_BODY(dir_no_note_link_mv_file_within, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(file_inside1, file_inside2) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, 0);
}
ATF_TC_CLEANUP(dir_no_note_link_mv_file_within, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_link_create_dir_in);
ATF_TC_HEAD(dir_note_link_create_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_LINK for the directory "
		"'foo' if a directory 'foo/bar' is created.");
}
ATF_TC_BODY(dir_note_link_create_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, NOTE_LINK);
}
ATF_TC_CLEANUP(dir_note_link_create_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_link_delete_dir_in);
ATF_TC_HEAD(dir_note_link_delete_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_LINK for the directory "
		"'foo' if a directory 'foo/bar' is deleted.");
}
ATF_TC_BODY(dir_note_link_delete_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rmdir(dir_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, NOTE_LINK);
}
ATF_TC_CLEANUP(dir_note_link_delete_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_link_mv_dir_in);
ATF_TC_HEAD(dir_note_link_mv_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_LINK for the directory "
		"'foo' if a directory 'bar' is renamed to 'foo/bar'.");
}
ATF_TC_BODY(dir_note_link_mv_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_outside, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_outside, dir_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, NOTE_LINK);
}
ATF_TC_CLEANUP(dir_note_link_mv_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_link_mv_dir_out);
ATF_TC_HEAD(dir_note_link_mv_dir_out, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_LINK for the directory "
		"'foo' if a directory 'foo/bar' is renamed to 'bar'.");
}
ATF_TC_BODY(dir_note_link_mv_dir_out, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_inside1, dir_outside) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_LINK, NOTE_LINK);
}
ATF_TC_CLEANUP(dir_note_link_mv_dir_out, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_create_dir_in);
ATF_TC_HEAD(dir_note_write_create_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a directory 'foo/bar' is created.");
}
ATF_TC_BODY(dir_note_write_create_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_create_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_create_file_in);
ATF_TC_HEAD(dir_note_write_create_file_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a file 'foo/baz' is created.");
}
ATF_TC_BODY(dir_note_write_create_file_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_create_file_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_delete_dir_in);
ATF_TC_HEAD(dir_note_write_delete_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a directory 'foo/bar' is deleted.");
}
ATF_TC_BODY(dir_note_write_delete_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rmdir(dir_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_delete_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_delete_file_in);
ATF_TC_HEAD(dir_note_write_delete_file_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a file 'foo/baz' is deleted.");
}
ATF_TC_BODY(dir_note_write_delete_file_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(unlink(file_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_delete_file_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_dir_in);
ATF_TC_HEAD(dir_note_write_mv_dir_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a directory 'bar' is renamed to 'foo/bar'.");
}
ATF_TC_BODY(dir_note_write_mv_dir_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_outside, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_outside, dir_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_dir_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_dir_out);
ATF_TC_HEAD(dir_note_write_mv_dir_out, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a directory 'foo/bar' is renamed to 'bar'.");
}
ATF_TC_BODY(dir_note_write_mv_dir_out, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_inside1, dir_outside) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_dir_out, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_dir_within);
ATF_TC_HEAD(dir_note_write_mv_dir_within, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a directory 'foo/bar' is renamed to 'foo/baz'.");
}
ATF_TC_BODY(dir_note_write_mv_dir_within, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(mkdir(dir_inside1, S_IRWXU) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(dir_inside1, dir_inside2) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_dir_within, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_file_in);
ATF_TC_HEAD(dir_note_write_mv_file_in, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a file 'qux' is renamed to 'foo/baz'.");
}
ATF_TC_BODY(dir_note_write_mv_file_in, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_outside) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(file_outside, file_inside1) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_file_in, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_file_out);
ATF_TC_HEAD(dir_note_write_mv_file_out, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a file 'foo/baz' is renamed to 'qux'.");
}
ATF_TC_BODY(dir_note_write_mv_file_out, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(file_inside1, file_outside) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_file_out, tc)
{
	cleanup();
}

ATF_TC_WITH_CLEANUP(dir_note_write_mv_file_within);
ATF_TC_HEAD(dir_note_write_mv_file_within, tc)
{
	atf_tc_set_md_var(tc, "descr", "This test case ensures "
		"that kevent(2) returns NOTE_WRITE for the directory "
		"'foo' if a file 'foo/baz' is renamed to 'foo/qux'.");
}
ATF_TC_BODY(dir_note_write_mv_file_within, tc)
{
	struct kevent changelist[1];

	ATF_REQUIRE(init_target() != -1);
	ATF_REQUIRE(create_file(file_inside1) != -1);
	ATF_REQUIRE(init_kqueue() != -1);

	ATF_REQUIRE(rename(file_inside1, file_inside2) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, changelist, 1, &ts) != -1);
	ATF_CHECK_EQ(changelist[0].fflags & NOTE_WRITE, NOTE_WRITE);
}
ATF_TC_CLEANUP(dir_note_write_mv_file_within, tc)
{
	cleanup();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, dir_no_note_link_create_file_in);
	ATF_TP_ADD_TC(tp, dir_no_note_link_delete_file_in);
	ATF_TP_ADD_TC(tp, dir_no_note_link_mv_dir_within);
	ATF_TP_ADD_TC(tp, dir_no_note_link_mv_file_within);
	ATF_TP_ADD_TC(tp, dir_note_link_create_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_link_delete_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_link_mv_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_link_mv_dir_out);
	ATF_TP_ADD_TC(tp, dir_note_write_create_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_write_create_file_in);
	ATF_TP_ADD_TC(tp, dir_note_write_delete_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_write_delete_file_in);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_dir_in);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_dir_out);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_dir_within);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_file_in);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_file_out);
	ATF_TP_ADD_TC(tp, dir_note_write_mv_file_within);
	return atf_no_error();
}
