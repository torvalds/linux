/*
 * main.c - ktap compiler and loader entry
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"


/*******************************************************************/

void *ktapc_reallocv(void *block, size_t osize, size_t nsize)
{
	return kp_reallocv(NULL, block, osize, nsize);
}

ktap_closure *ktapc_newlclosure(int n)
{
	return kp_newlclosure(NULL, n);
}

ktap_proto *ktapc_newproto()
{
	return kp_newproto(NULL);
}

const ktap_value *ktapc_table_get(ktap_table *t, const ktap_value *key)
{
	return kp_table_get(t, key);
}

void ktapc_table_setvalue(ktap_table *t, const ktap_value *key, ktap_value *val)
{
	kp_table_setvalue(NULL, t, key, val);
}

ktap_table *ktapc_table_new()
{
	return kp_table_new(NULL);
}

ktap_string *ktapc_ts_newlstr(const char *str, size_t l)
{
	return kp_tstring_newlstr(NULL, str, l);
}

ktap_string *ktapc_ts_new(const char *str)
{
	return kp_tstring_new(NULL, str);
}

int ktapc_ts_eqstr(ktap_string *a, ktap_string *b)
{
	return kp_tstring_eqstr(a, b);
}

static void ktapc_runerror(const char *err_msg_fmt, ...)
{
	va_list ap;

	fprintf(stderr, "ktapc_runerror\n");

	va_start(ap, err_msg_fmt);
	vfprintf(stderr, err_msg_fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

/*
 * todo: memory leak here
 */
char *ktapc_sprintf(const char *fmt, ...)
{
	char *msg = malloc(128);

	va_list argp;
	va_start(argp, fmt);
	vsprintf(msg, fmt, argp);
	va_end(argp);
	return msg;
}


#define MINSIZEARRAY	4

void *ktapc_growaux(void *block, int *size, size_t size_elems, int limit,
		    const char *what)
{
	void *newblock;
	int newsize;

	if (*size >= limit/2) {  /* cannot double it? */
		if (*size >= limit)  /* cannot grow even a little? */
			ktapc_runerror("too many %s (limit is %d)\n",
					what, limit);
		newsize = limit;  /* still have at least one free place */
	} else {
		newsize = (*size) * 2;
		if (newsize < MINSIZEARRAY)
			newsize = MINSIZEARRAY;  /* minimum size */
	}

	newblock = ktapc_reallocv(block, (*size) * size_elems, newsize * size_elems);
	*size = newsize;  /* update only when everything else is OK */
	return newblock;
}

/*************************************************************************/

#define print_base(i) \
	do {	\
		if (i < f->sizelocvars) /* it's a localvars */ \
			printf("%s", getstr(f->locvars[i].varname));  \
		else \
			printf("base + %d", i);	\
	} while (0)

#define print_RKC(instr)	\
	do {	\
		if (ISK(GETARG_C(instr))) \
			kp_showobj(NULL, k + INDEXK(GETARG_C(instr))); \
		else \
			print_base(GETARG_C(instr)); \
	} while (0)

static void decode_instruction(ktap_proto *f, int instr)
{
	int opcode = GET_OPCODE(instr);
	ktap_value *k;

	k = f->k;

	printf("%.8x\t", instr);
	printf("%s\t", ktap_opnames[opcode]);

	switch (opcode) {
	case OP_GETTABUP:
		print_base(GETARG_A(instr));
		printf(" <- ");

		if (GETARG_B(instr) == 0)
			printf("global");
		else
			printf("upvalues[%d]", GETARG_B(instr));

		printf("{"); print_RKC(instr); printf("}");

		break;
	case OP_GETTABLE:
		print_base(GETARG_A(instr));
		printf(" <- ");

		print_base(GETARG_B(instr));

		printf("{");
		print_RKC(instr);
		printf("}");
		break;
	case OP_LOADK:
		printf("\t");
		print_base(GETARG_A(instr));
		printf(" <- ");

		kp_showobj(NULL, k + GETARG_Bx(instr));
		break;
	case OP_CALL:
		printf("\t");
		print_base(GETARG_A(instr));
		break;
	case OP_JMP:
		printf("\t%d", GETARG_sBx(instr));
		break;
	default:
		break;
	}

	printf("\n");
}

static int function_nr = 0;

/* this is a debug function used for check bytecode chunk file */
static void dump_function(int level, ktap_proto *f)
{
	int i;

	printf("\n----------------------------------------------------\n");
	printf("function %d [level %d]:\n", function_nr++, level);
	printf("linedefined: %d\n", f->linedefined);
	printf("lastlinedefined: %d\n", f->lastlinedefined);
	printf("numparams: %d\n", f->numparams);
	printf("is_vararg: %d\n", f->is_vararg);
	printf("maxstacksize: %d\n", f->maxstacksize);
	printf("source: %s\n", getstr(f->source));
	printf("sizelineinfo: %d \t", f->sizelineinfo);
	for (i = 0; i < f->sizelineinfo; i++)
		printf("%d ", f->lineinfo[i]);
	printf("\n");

	printf("sizek: %d\n", f->sizek);
	for (i = 0; i < f->sizek; i++) {
		switch(f->k[i].type) {
		case KTAP_TNIL:
			printf("\tNIL\n");
			break;
		case KTAP_TBOOLEAN:
			printf("\tBOOLEAN: ");
			printf("%d\n", f->k[i].val.b);
			break;
		case KTAP_TNUMBER:
			printf("\tTNUMBER: ");
			printf("%ld\n", f->k[i].val.n);
			break;
		case KTAP_TSTRING:
			printf("\tTSTRING: ");
			printf("%s\n", svalue(&(f->k[i])));

			break;
		default:
			printf("\terror: unknow constant type\n");
		}
	}

	printf("sizelocvars: %d\n", f->sizelocvars);
	for (i = 0; i < f->sizelocvars; i++) {
		printf("\tlocvars: %s startpc: %d endpc: %d\n",
			getstr(f->locvars[i].varname), f->locvars[i].startpc,
			f->locvars[i].endpc);
	}

	printf("sizeupvalues: %d\n", f->sizeupvalues);
	for (i = 0; i < f->sizeupvalues; i++) {
		printf("\tname: %s instack: %d idx: %d\n",
			getstr(f->upvalues[i].name), f->upvalues[i].instack,
			f->upvalues[i].idx);
	}

	printf("\n");
	printf("sizecode: %d\n", f->sizecode);
	for (i = 0; i < f->sizecode; i++)
		decode_instruction(f, f->code[i]);

	printf("sizep: %d\n", f->sizep);
	for (i = 0; i < f->sizep; i++)
		dump_function(level + 1, f->p[i]);

}

static void usage(const char *msg_fmt, ...)
{
	va_list ap;

	va_start(ap, msg_fmt);
	fprintf(stderr, msg_fmt, ap);
	va_end(ap);

	fprintf(stderr,
"Usage: ktap [options] file [script args] -- cmd [args]\n"
"   or: ktap [options] -e one-liner  -- cmd [args]\n"
"\n"
"Options and arguments:\n"
"  -o file        : send script output to file, instead of stderr\n"
"  -p pid         : specific tracing pid\n"
"  -C cpu         : cpu to monitor in system-wide\n"
"  -T             : show timestamp for event\n"
"  -V             : show version\n"
"  -v             : enable verbose mode\n"
"  -s             : simple event tracing\n"
"  -b             : list byte codes\n"
"  file           : program read from script file\n"
"  -- cmd [args]  : workload to tracing\n");

	exit(EXIT_FAILURE);
}

ktap_global_state dummy_global_state;

static void init_dummy_global_state()
{
	memset(&dummy_global_state, 0, sizeof(ktap_global_state));
	dummy_global_state.seed = 201236;

        kp_tstring_resize(NULL, 32); /* set inital string hashtable size */
}

#define handle_error(str) do { perror(str); exit(-1); } while(0)

static struct ktap_parm uparm;
static int ktap_trunk_mem_size = 1024;

static int ktapc_writer(const void* p, size_t sz, void* ud)
{
	if (uparm.trunk_len + sz > ktap_trunk_mem_size) {
		int new_size = (uparm.trunk_len + sz) * 2;
		uparm.trunk = realloc(uparm.trunk, new_size);
		ktap_trunk_mem_size = new_size;
	}

	memcpy(uparm.trunk + uparm.trunk_len, p, sz);
	uparm.trunk_len += sz;

	return 0;
}


static int forks;
static char **workload_argv;

static int fork_workload(int ktap_fd)
{
	int pid;

	pid = fork();
	if (pid < 0)
		handle_error("failed to fork");

	if (pid > 0)
		return pid;

	signal(SIGTERM, SIG_DFL);

	execvp("", workload_argv);

	/*
	 * waiting ktapvm prepare all tracing event
	 * make it more robust in future.
	 */
	pause();

	execvp(workload_argv[0], workload_argv);

	perror(workload_argv[0]);
	exit(-1);

	return -1;
}

#define KTAPVM_PATH "/sys/kernel/debug/ktap/ktapvm"

static char *output_filename;
pid_t ktap_pid;

static int run_ktapvm()
{
        int ktapvm_fd, ktap_fd;
	int ret;

	ktap_pid = getpid();

	ktapvm_fd = open(KTAPVM_PATH, O_RDONLY);
	if (ktapvm_fd < 0)
		handle_error("open " KTAPVM_PATH " failed");

	ktap_fd = ioctl(ktapvm_fd, 0, NULL);
	if (ktap_fd < 0)
		handle_error("ioctl ktapvm failed");

	ktapio_create(output_filename);

	if (forks) {
		uparm.trace_pid = fork_workload(ktap_fd);
		uparm.workload = 1;
	}

	ret = ioctl(ktap_fd, KTAP_CMD_IOC_RUN, &uparm);

	close(ktap_fd);
	close(ktapvm_fd);

	return ret;
}

int verbose;
static int dump_bytecode;
static char oneline_src[1024];
static int trace_pid = -1;
static int trace_cpu = -1;
static int print_timestamp;

#define SIMPLE_ONE_LINER_FMT	\
	"trace %s { print(cpu(), tid(), execname(), argevent) }"

static const char *script_file;
static int script_args_start;
static int script_args_end;

static void parse_option(int argc, char **argv)
{
	char pid[32] = {0};
	char cpu_str[32] = {0};
	char *next_arg;
	int i, j;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			script_file = argv[i];
			if (!script_file)
				usage("");

			script_args_start = i + 1;
			script_args_end = argc;

			for (j = i + 1; j < argc; j++) {
				if (argv[j][0] == '-' && argv[j][1] == '-')
					goto found_cmd;
			}

			return;
		}

		if (argv[i][0] == '-' && argv[i][1] == '-') {
			j = i;
			goto found_cmd;
		}

		next_arg = argv[i + 1];

		switch (argv[i][1]) {
		case 'o':
			output_filename = malloc(strlen(next_arg) + 1);
			if (!output_filename)
				return;

			strncpy(output_filename, next_arg, strlen(next_arg));
			i++;
			break;
		case 'e':
			strncpy(oneline_src, next_arg, strlen(next_arg));
			i++;
			break;
		case 'p':
			strncpy(pid, next_arg, strlen(next_arg));
			trace_pid = atoi(pid);
			i++;
			break;
		case 'C':
			strncpy(cpu_str, next_arg, strlen(next_arg));
			trace_cpu = atoi(cpu_str);
			i++;
			break;
		case 'T':
			print_timestamp = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
			sprintf(oneline_src, SIMPLE_ONE_LINER_FMT, next_arg);
			i++;
			break;
		case 'b':
			dump_bytecode = 1;
			break;
		case 'V':
		case '?':
		case 'h':
			usage("");
			break;
		default:
			usage("wrong argument\n");
			break;
		}
	}

	return;

 found_cmd:
	script_args_end = j;
	forks = 1;
	workload_argv = &argv[j + 1];
}

static void compile(const char *input)
{
	ktap_closure *cl;
	char *buff;
	struct stat sb;
	int fdin;

	if (oneline_src[0] != '\0') {
		init_dummy_global_state();
		cl = ktapc_parser(oneline_src, input);
		goto dump;
	}

	fdin = open(input, O_RDONLY);
	if (fdin < 0) {
		fprintf(stderr, "open file %s failed\n", input);
		exit(-1);
	}

	if (fstat(fdin, &sb) == -1)
		handle_error("fstat failed");

	buff = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fdin, 0);
	if (buff == MAP_FAILED)
		handle_error("mmap failed");

	init_dummy_global_state();
	cl = ktapc_parser(buff, input);

	munmap(buff, sb.st_size);
	close(fdin);

 dump:
	if (dump_bytecode) {
		dump_function(1, cl->l.p);
		exit(0);
	}

	/* ktapc output */
	uparm.trunk = malloc(ktap_trunk_mem_size);
	if (!uparm.trunk)
		handle_error("malloc failed");

	ktapc_dump(cl->l.p, ktapc_writer, NULL, 0);
}

int main(int argc, char **argv)
{
	char **ktapvm_argv;
	int new_index, i;
	int ret;

	if (argc == 1)
		usage("");

	parse_option(argc, argv);

	if (oneline_src[0] != '\0')
		script_file = "one-liner";

	compile(script_file);

	ktapvm_argv = (char **)malloc(sizeof(char *)*(script_args_end -
					script_args_start + 1));
	if (!ktapvm_argv) {
		fprintf(stderr, "canno allocate ktapvm_argv\n");
		return -1;
	}

	ktapvm_argv[0] = malloc(strlen(script_file) + 1);
	if (!ktapvm_argv[0]) {
		fprintf(stderr, "canno allocate memory\n");
		return -1;
	}
	strcpy(ktapvm_argv[0], script_file);
	ktapvm_argv[0][strlen(script_file)] = '\0';

	/* pass rest argv into ktapvm */
	new_index = 1;
	for (i = script_args_start; i < script_args_end; i++) {
		ktapvm_argv[new_index] = malloc(strlen(argv[i]) + 1);
		if (!ktapvm_argv[new_index]) {
			fprintf(stderr, "canno allocate memory\n");
			return -1;
		}
		strcpy(ktapvm_argv[new_index], argv[i]);
		ktapvm_argv[new_index][strlen(argv[i])] = '\0';
		new_index++;
	}

	uparm.argv = ktapvm_argv;
	uparm.argc = new_index;
	uparm.verbose = verbose;
	uparm.trace_pid = trace_pid;
	uparm.trace_cpu = trace_cpu;
	uparm.print_timestamp = print_timestamp;

	/* start running into kernel ktapvm */
	ret = run_ktapvm();

	cleanup_event_resources();
	return ret;
}


