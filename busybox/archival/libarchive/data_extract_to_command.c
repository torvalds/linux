/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

enum {
	//TAR_FILETYPE,
	TAR_MODE,
	TAR_FILENAME,
	TAR_REALNAME,
#if ENABLE_FEATURE_TAR_UNAME_GNAME
	TAR_UNAME,
	TAR_GNAME,
#endif
	TAR_SIZE,
	TAR_UID,
	TAR_GID,
	TAR_MAX,
};

static const char *const tar_var[] = {
	// "FILETYPE",
	"MODE",
	"FILENAME",
	"REALNAME",
#if ENABLE_FEATURE_TAR_UNAME_GNAME
	"UNAME",
	"GNAME",
#endif
	"SIZE",
	"UID",
	"GID",
};

static void xputenv(char *str)
{
	if (putenv(str))
		bb_die_memory_exhausted();
}

static void str2env(char *env[], int idx, const char *str)
{
	env[idx] = xasprintf("TAR_%s=%s", tar_var[idx], str);
	xputenv(env[idx]);
}

static void dec2env(char *env[], int idx, unsigned long long val)
{
	env[idx] = xasprintf("TAR_%s=%llu", tar_var[idx], val);
	xputenv(env[idx]);
}

static void oct2env(char *env[], int idx, unsigned long val)
{
	env[idx] = xasprintf("TAR_%s=%lo", tar_var[idx], val);
	xputenv(env[idx]);
}

void FAST_FUNC data_extract_to_command(archive_handle_t *archive_handle)
{
	file_header_t *file_header = archive_handle->file_header;

#if 0 /* do we need this? ENABLE_FEATURE_TAR_SELINUX */
	char *sctx = archive_handle->tar__sctx[PAX_NEXT_FILE];
	if (!sctx)
		sctx = archive_handle->tar__sctx[PAX_GLOBAL];
	if (sctx) { /* setfscreatecon is 4 syscalls, avoid if possible */
		setfscreatecon(sctx);
		free(archive_handle->tar__sctx[PAX_NEXT_FILE]);
		archive_handle->tar__sctx[PAX_NEXT_FILE] = NULL;
	}
#endif

	if ((file_header->mode & S_IFMT) == S_IFREG) {
		pid_t pid;
		int p[2], status;
		char *tar_env[TAR_MAX];

		memset(tar_env, 0, sizeof(tar_env));

		xpipe(p);
		pid = BB_MMU ? xfork() : xvfork();
		if (pid == 0) {
			/* Child */
			/* str2env(tar_env, TAR_FILETYPE, "f"); - parent should do it once */
			oct2env(tar_env, TAR_MODE, file_header->mode);
			str2env(tar_env, TAR_FILENAME, file_header->name);
			str2env(tar_env, TAR_REALNAME, file_header->name);
#if ENABLE_FEATURE_TAR_UNAME_GNAME
			str2env(tar_env, TAR_UNAME, file_header->tar__uname);
			str2env(tar_env, TAR_GNAME, file_header->tar__gname);
#endif
			dec2env(tar_env, TAR_SIZE, file_header->size);
			dec2env(tar_env, TAR_UID, file_header->uid);
			dec2env(tar_env, TAR_GID, file_header->gid);
			close(p[1]);
			xdup2(p[0], STDIN_FILENO);
			signal(SIGPIPE, SIG_DFL);
			execl(archive_handle->tar__to_command_shell,
				archive_handle->tar__to_command_shell,
				"-c",
				archive_handle->tar__to_command,
				(char *)0);
			bb_perror_msg_and_die("can't execute '%s'", archive_handle->tar__to_command_shell);
		}
		close(p[0]);
		/* Our caller is expected to do signal(SIGPIPE, SIG_IGN)
		 * so that we don't die if child don't read all the input: */
		bb_copyfd_exact_size(archive_handle->src_fd, p[1], -file_header->size);
		close(p[1]);

		status = wait_for_exitstatus(pid);
		if (WIFEXITED(status) && WEXITSTATUS(status))
			bb_error_msg_and_die("'%s' returned status %d",
				archive_handle->tar__to_command, WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			bb_error_msg_and_die("'%s' terminated by signal %d",
				archive_handle->tar__to_command, WTERMSIG(status));

		if (!BB_MMU) {
			int i;
			for (i = 0; i < TAR_MAX; i++) {
				if (tar_env[i])
					bb_unsetenv_and_free(tar_env[i]);
			}
		}
	}

#if 0 /* ENABLE_FEATURE_TAR_SELINUX */
	if (sctx)
		/* reset the context after creating an entry */
		setfscreatecon(NULL);
#endif
}
