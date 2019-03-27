/* Copyright (C) 1995 Florian La Roche */
/* Who wants to help coding? I don't like doing this... */

/* You can just start setup as normal user and see how far it is coded
   right now. This will do a fake installation and won't actually chnage
   any data on your computer. */

/* TODO: write a good package selection code
         change functions to return better error code
 */

/* Show an extra text-box with the contents of all external commands,
   before they are executed. So you can abort the installation, if any
   wrong commands are to be executed. (So don't format wrong partition.) */
#define VERBOSE 1

/* If defined, don't actually execute any comands and don't actually modify
   any files. So you can test any possible installation without doing any
   damage to your computer.
   The file FDISK.TEST is used instead of real "fdisk -l" output, so that
   it can be started as normal user. */
#define DEBUG_THIS 1

#include <dialog.h>

/* max length of a partition name like e.g. '/dev/hda1' */
#define MAX_DEV_NAME 25

/* max number of possible Linux/Swap/MsDos partitions */
#define MAX_PARTS 20

char *progname = NULL;

static void
error(const char *s)
{
    fprintf(stderr, "%s: %s\n", progname, s);
    exit(1);
}

static int
my_system(const char *s,...)
{
    int ret, i;
    va_list ap;
    char sh[200];

    va_start(ap, s);
    vsprintf(sh, s, ap);
    va_end(ap);

#ifdef	VERBOSE
    i = dialog_msgbox("I will run the following command:", sh, 10, 65, 1);
    dialog_clear();
#ifdef DEBUG_THIS
    return 0;
#endif
#endif
    ret = system(sh);
    if (!(ret >> 8))
	return 0;
    i = dialog_msgbox("Error-Exit on the following command:",
		      sh, 12, 73, 1);
    dialog_clear();
    return 1;
}

/* We support to install from DOS/Linux-partitions. */
enum partition_type {
    MsDos,
    Linux,
    Swap
};

struct partition {
    enum partition_type type;
    char name[MAX_DEV_NAME];
    int blocks;
    int flag;
} partitions[MAX_PARTS];
int num_partition = 0;
int num_linux = 0;
int num_swap = 0;
int num_msdos = 0;

static int
get_line(char *line, int size, FILE * f)
{
    char *ptr = line;
    int c;

    if (feof(f))
	return -1;
    while (size-- && ((c = getc(f)) != EOF) && (c != '\n'))
	*ptr++ = c;
    *ptr++ = '\0';
    return (int) (ptr - line);
}

static void
read_partitions(void)
{
    FILE *f;
    char line[200];
    int length;
#ifndef DEBUG_THIS
    int ret = system("fdisk -l 2>/dev/null 1>/tmp/fdisk.output");
    if ((ret >> 8) != 0) {
	error("fdisk didn't run");
    }
    if ((f = fopen("/tmp/fdisk.output", "r")) == NULL)
#else
    if ((f = fopen("FDISK.TEST", "r")) == NULL)
#endif
	error("cannot read fdisk output");

    while (num_partition <= MAX_PARTS
	   && (length = get_line(line, 200, f)) >= 0) {
	if (strncmp(line, "/dev/", 5) == 0) {
	    int n = 0;
	    char *s = line + 5;
	    char *t = partitions[num_partition].name;
	    strcpy(t, "/dev/");
	    t += 5;
	    while (n < MAX_DEV_NAME && *s != '\0'
		   && !isspace((unsigned char) *s)) {
		*t++ = *s++;
		n++;
	    }
	    *t = '\0';
	    /* Read the size of the partition. */
	    t = line + 37;
	    while (isspace((unsigned char) *t))
		t++;
	    partitions[num_partition].blocks = atoi(t);
	    if (strstr(line, "Linux native")) {
		partitions[num_partition].type = Linux;
		num_partition++;
		num_linux++;
	    } else if (strstr(line, "Linux swap")) {
		partitions[num_partition].type = Swap;
		num_partition++;
		num_swap++;
	    } else if (strstr(line, "DOS")) {
		partitions[num_partition].type = MsDos;
		num_partition++;
		num_msdos++;
	    }
	}
    }
    fclose(f);
#ifndef DEBUG_THIS
    unlink("/tmp/fdisk.output");
#endif
}

static int
select_partition(const char *title, const char *prompt, int y, int x)
{
    int i, num, ret;
    char info[MAX_PARTS][40];
    char *items[MAX_PARTS * 2];
    int num_pa[MAX_PARTS];

    num = 0;
    for (i = 0; i < num_partition; i++) {
	if (partitions[i].type == Linux) {
	    items[num * 2] = partitions[i].name;
	    sprintf(info[num], "Linux partition with %d blocks",
		    partitions[i].blocks);
	    items[num * 2 + 1] = info[num];
	    num_pa[num] = i;
	    num++;
	}
    }
    ret = dialog_menu(title, prompt, y + num, x, num, num, items);
    dialog_clear();
    if (ret >= 0)		/* item selected */
	ret = num_pa[ret];
    return ret;
}

static int
select_install_partition(void)
{
    return select_partition("Select Install Partition",
			    "\\nWhere do you want to install Linux?\\n", 9, 60);
}

static int
select_source_partition(void)
{
    return select_partition("Select Source Partition",
			    "\\nOn which partition is the source?\\n", 9, 60);
}

const char *null = ">/dev/null 2>/dev/null";
const char *install_partition = NULL;

static void
extract_packages(const char *source_path)
{
#ifndef	DEBUG_THIS
    FILE *f;
#endif

    if (my_system("mkdir -p /install/var/installed/packages %s", null))
	return;
    if (my_system("cd /install; for i in /source%s/*.tgz; do "
		  "tar xzplvvkf $i >> var/installed/packages/base "
		  "2>>var/installed/packages/ERROR; done", source_path))
	return;
#ifndef	DEBUG_THIS
    if ((f = fopen("/install/etc/fstab", "w")) == NULL) {
	/* i = */ dialog_msgbox("Error", "Cannot write /etc/fstab",
				12, 40, 1);
	return;
    }
    fprintf(f, "%s / ext2 defaults 1 1\n", install_partition);
    fprintf(f, "none /proc proc defaults 0 2\n");
    /* XXX write swap-partitions */
    fclose(f);
#endif
}

static void
install_premounted(void)
{
    extract_packages("");
}

static void
install_harddisk(void)
{
    const char *name;
    int part, ret;

    if ((part = select_source_partition()) <= -1)
	return;
    name = partitions[part].name;

    if (my_system("mount -t ext2 %s /source %s", name, null))
	return;
    ret = dialog_inputbox("Path in partition",
			  "Please enter the directory in which the "
			  "source files are.", 13, 50, "", FALSE);
    dialog_clear();
    if (ret != 0)
	return;
    /* XXX strdup */
    extract_packages(strdup(dialog_input_result));
    if (my_system("umount /source %s", null))
	return;
}

static void
install_nfs(void)
{
    if (my_system("ifconfig eth0 134.96.81.36 netmask 255.255.255.224 "
		  "broadcast 134.96.81.63 %s", null))
	return;
    if (my_system("route add -net 134.96.81.32 %s", null))
	return;
    if (my_system("mount -t nfs 134.96.81.38:"
		  "/local/ftp/pub/linux/ELF.binary/tar /source %s", null))
	return;
    extract_packages("/base");
    if (my_system("umount /source %s", null))
	return;
    if (my_system("ifconfig eth0 down %s", null))
	return;
}

static void
main_install(void)
{
    int part, ret;
    const char *name;
    char *items1[] =
    {
	"1", "Harddisk Install",
	"2", "Network Install(NFS)",
	"3", "Premounted on /source"
    };

    if (num_linux == 0) {
	/* XXX */
	return;
    }
    if ((part = select_install_partition()) <= -1)
	return;
    install_partition = name = partitions[part].name;
    if (my_system("mke2fs %s %s", name, null))
	return;
    if (my_system("mount -t ext2 %s /install %s", name, null))
	return;
    ret = dialog_menu("Choose install medium",
		      "\\nPlease say from where you want to install.\\n",
		      12, 62, 3, 3, items1);
    dialog_clear();
    switch (ret) {
    case 0:
	install_harddisk();
	break;
    case 1:
	install_nfs();
	break;
    case 2:
	install_premounted();
	break;
    case -2:			/* cancel */
    case -1:
	break;			/* esc */
    }
    if (my_system("umount /install %s", null))
	return;
}

int
main(int argc, char **argv)
{
    int stop = 0;
    int ret;
    char *items1[] =
    {
	"1", "Display a help text",
	"2", "Start an installation",
	"3", "Exit to the shell"
    };

    progname = argv[0];

    read_partitions();
    if (num_linux == 0) {
	printf("\n\nPlease start \"fdisk\" or \"cfdisk\" and create a"
	       "\nnative Linux-partition to install Linux on.\n\n");
	exit(1);
    }

    init_dialog();

    while (!stop) {
	ret = dialog_menu("Linux Install Utility",
			  "\\nCopyright (C) 1995 Florian La Roche\\n"
			  "\\nPre-Alpha version, be careful, read the doc!!!"
			  "\\nemail: florian@jurix.jura.uni-sb.de, "
			  "flla@stud.uni-sb.de\\n",
			  15, 64, 3, 3, items1);
	dialog_clear();
	switch (ret) {
	case 0:
	    ret = dialog_textbox("Help Text",
				 "setup.help", 20, 70);
	    dialog_clear();
	    break;
	case 1:
	    main_install();
	    break;
	case 2:
	    stop = 1;
	    break;
	case -2:		/* cancel */
	case -1:
	    stop = 1;		/* esc */
	}
    }
    end_dialog();
    printf("\nExecute \"reboot\" to restart your computer...\n");

    exit(0);
}
