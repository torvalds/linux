/*-
 * Copyright (c) 2014 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

#include <netinet/in.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <assert.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <assert.h>

#include "hv_kvp.h"
#include "hv_utilreg.h"
typedef uint8_t		__u8;
typedef uint16_t	__u16;
typedef uint32_t	__u32;
typedef uint64_t	__u64;

#define POOL_FILE_MODE	(S_IRUSR | S_IWUSR)
#define POOL_DIR_MODE	(POOL_FILE_MODE | S_IXUSR)
#define POOL_DIR	"/var/db/hyperv/pool"

/*
 * ENUM Data
 */

enum key_index {
	FullyQualifiedDomainName = 0,
	IntegrationServicesVersion, /*This key is serviced in the kernel*/
	NetworkAddressIPv4,
	NetworkAddressIPv6,
	OSBuildNumber,
	OSName,
	OSMajorVersion,
	OSMinorVersion,
	OSVersion,
	ProcessorArchitecture
};


enum {
	IPADDR = 0,
	NETMASK,
	GATEWAY,
	DNS
};


/* Global Variables */

/*
 * The structure for operation handlers.
 */
struct kvp_op_hdlr {
	int	kvp_op_key;
	void	(*kvp_op_init)(void);
 	int	(*kvp_op_exec)(struct hv_kvp_msg *kvp_op_msg, void *data);
};

static struct kvp_op_hdlr kvp_op_hdlrs[HV_KVP_OP_COUNT];

/* OS information */

static const char *os_name = "";
static const char *os_major = "";
static const char *os_minor = "";
static const char *processor_arch;
static const char *os_build;
static const char *lic_version = "BSD Pre-Release version";
static struct utsname uts_buf;

/* Global flags */
static int is_daemon = 1;
static int is_debugging = 0;

#define	KVP_LOG(priority, format, args...) do	{			\
		if (is_debugging == 1) {				\
			if (is_daemon == 1)				\
				syslog(priority, format, ## args);	\
			else						\
				printf(format, ## args);		\
		} else {						\
			if (priority < LOG_DEBUG) {			\
				if (is_daemon == 1)			\
					syslog(priority, format, ## args);	\
				else					\
					printf(format, ## args);	\
			}						\
		}							\
	} while(0)

/*
 * For KVP pool file
 */

#define MAX_FILE_NAME		100
#define ENTRIES_PER_BLOCK	50

struct kvp_record {
	char	key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
	char	value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE];
};

struct kvp_pool {
	int			pool_fd;
	int			num_blocks;
	struct kvp_record	*records;
	int			num_records;
	char			fname[MAX_FILE_NAME];
};

static struct kvp_pool kvp_pools[HV_KVP_POOL_COUNT];


static void
kvp_acquire_lock(int pool)
{
	struct flock fl = { 0, 0, 0, F_WRLCK, SEEK_SET, 0 };

	fl.l_pid = getpid();

	if (fcntl(kvp_pools[pool].pool_fd, F_SETLKW, &fl) == -1) {
		KVP_LOG(LOG_ERR, "Failed to acquire the lock pool: %d", pool);
		exit(EXIT_FAILURE);
	}
}


static void
kvp_release_lock(int pool)
{
	struct flock fl = { 0, 0, 0, F_UNLCK, SEEK_SET, 0 };

	fl.l_pid = getpid();

	if (fcntl(kvp_pools[pool].pool_fd, F_SETLK, &fl) == -1) {
		perror("fcntl");
		KVP_LOG(LOG_ERR, "Failed to release the lock pool: %d\n", pool);
		exit(EXIT_FAILURE);
	}
}


/*
 * Write in-memory copy of KVP to pool files
 */
static void
kvp_update_file(int pool)
{
	FILE *filep;
	size_t bytes_written;

	kvp_acquire_lock(pool);

	filep = fopen(kvp_pools[pool].fname, "w");
	if (!filep) {
		kvp_release_lock(pool);
		KVP_LOG(LOG_ERR, "Failed to open file, pool: %d\n", pool);
		exit(EXIT_FAILURE);
	}

	bytes_written = fwrite(kvp_pools[pool].records,
		sizeof(struct kvp_record),
		kvp_pools[pool].num_records, filep);

	if (ferror(filep) || fclose(filep)) {
		kvp_release_lock(pool);
		KVP_LOG(LOG_ERR, "Failed to write file, pool: %d\n", pool);
		exit(EXIT_FAILURE);
	}

	kvp_release_lock(pool);
}


/*
 * Read KVPs from pool files and store in memory
 */
static void
kvp_update_mem_state(int pool)
{
	FILE *filep;
	size_t records_read = 0;
	struct kvp_record *record = kvp_pools[pool].records;
	struct kvp_record *readp;
	int num_blocks = kvp_pools[pool].num_blocks;
	int alloc_unit = sizeof(struct kvp_record) * ENTRIES_PER_BLOCK;

	kvp_acquire_lock(pool);

	filep = fopen(kvp_pools[pool].fname, "r");
	if (!filep) {
		kvp_release_lock(pool);
		KVP_LOG(LOG_ERR, "Failed to open file, pool: %d\n", pool);
		exit(EXIT_FAILURE);
	}
	for ( ; ; )
	{
		readp = &record[records_read];
		records_read += fread(readp, sizeof(struct kvp_record),
			ENTRIES_PER_BLOCK * num_blocks,
			filep);

		if (ferror(filep)) {
			KVP_LOG(LOG_ERR, "Failed to read file, pool: %d\n", pool);
			exit(EXIT_FAILURE);
		}

		if (!feof(filep)) {
			/*
			 * Have more data to read. Expand the memory.
			 */
			num_blocks++;
			record = realloc(record, alloc_unit * num_blocks);

			if (record == NULL) {
				KVP_LOG(LOG_ERR, "malloc failed\n");
				exit(EXIT_FAILURE);
			}
			continue;
		}
		break;
	}

	kvp_pools[pool].num_blocks = num_blocks;
	kvp_pools[pool].records = record;
	kvp_pools[pool].num_records = records_read;

	fclose(filep);
	kvp_release_lock(pool);
}


static int
kvp_file_init(void)
{
	int fd;
	FILE *filep;
	size_t records_read;
	char *fname;
	struct kvp_record *record;
	struct kvp_record *readp;
	int num_blocks;
	int i;
	int alloc_unit = sizeof(struct kvp_record) * ENTRIES_PER_BLOCK;

	if (mkdir(POOL_DIR, POOL_DIR_MODE) < 0 &&
	    (errno != EEXIST && errno != EISDIR)) {
		KVP_LOG(LOG_ERR, " Failed to create /var/db/hyperv/pool\n");
		exit(EXIT_FAILURE);
	}
	chmod(POOL_DIR, POOL_DIR_MODE); /* fix old mistake */

	for (i = 0; i < HV_KVP_POOL_COUNT; i++)
	{
		fname = kvp_pools[i].fname;
		records_read = 0;
		num_blocks = 1;
		snprintf(fname, MAX_FILE_NAME, "/var/db/hyperv/pool/.kvp_pool_%d", i);
		fd = open(fname, O_RDWR | O_CREAT, POOL_FILE_MODE);

		if (fd == -1) {
			return (1);
		}
		fchmod(fd, POOL_FILE_MODE); /* fix old mistake */


		filep = fopen(fname, "r");
		if (!filep) {
			close(fd);
			return (1);
		}

		record = malloc(alloc_unit * num_blocks);
		if (record == NULL) {
			close(fd);
			fclose(filep);
			return (1);
		}
		for ( ; ; )
		{
			readp = &record[records_read];
			records_read += fread(readp, sizeof(struct kvp_record),
				ENTRIES_PER_BLOCK,
				filep);

			if (ferror(filep)) {
				KVP_LOG(LOG_ERR, "Failed to read file, pool: %d\n",
				    i);
				exit(EXIT_FAILURE);
			}

			if (!feof(filep)) {
				/*
				 * More data to read.
				 */
				num_blocks++;
				record = realloc(record, alloc_unit *
					num_blocks);
				if (record == NULL) {
					close(fd);
					fclose(filep);
					return (1);
				}
				continue;
			}
			break;
		}
		kvp_pools[i].pool_fd = fd;
		kvp_pools[i].num_blocks = num_blocks;
		kvp_pools[i].records = record;
		kvp_pools[i].num_records = records_read;
		fclose(filep);
	}

	return (0);
}


static int
kvp_key_delete(int pool, __u8 *key, int key_size)
{
	int i;
	int j, k;
	int num_records;
	struct kvp_record *record;

	KVP_LOG(LOG_DEBUG, "kvp_key_delete: pool =  %d, "
	    "key = %s\n", pool, key);

	/* Update in-memory state */
	kvp_update_mem_state(pool);

	num_records = kvp_pools[pool].num_records;
	record = kvp_pools[pool].records;

	for (i = 0; i < num_records; i++)
	{
		if (memcmp(key, record[i].key, key_size)) {
			continue;
		}

		KVP_LOG(LOG_DEBUG, "Found delete key in pool %d.\n",
		    pool);
		/*
		 * We found a match at the end; Just update the number of
		 * entries and we are done.
		 */
		if (i == num_records) {
			kvp_pools[pool].num_records--;
			kvp_update_file(pool);
			return (0);
		}

		/*
		 * We found a match in the middle; Move the remaining
		 * entries up.
		 */
		j = i;
		k = j + 1;
		for ( ; k < num_records; k++)
		{
			strcpy(record[j].key, record[k].key);
			strcpy(record[j].value, record[k].value);
			j++;
		}
		kvp_pools[pool].num_records--;
		kvp_update_file(pool);
		return (0);
	}
	KVP_LOG(LOG_DEBUG, "Not found delete key in pool %d.\n",
	    pool);
	return (1);
}


static int
kvp_key_add_or_modify(int pool, __u8 *key, __u32 key_size, __u8 *value,
    __u32 value_size)
{
	int i;
	int num_records;
	struct kvp_record *record;
	int num_blocks;

	KVP_LOG(LOG_DEBUG, "kvp_key_add_or_modify: pool =  %d, "
	    "key = %s, value = %s\n,", pool, key, value);

	if ((key_size > HV_KVP_EXCHANGE_MAX_KEY_SIZE) ||
	    (value_size > HV_KVP_EXCHANGE_MAX_VALUE_SIZE)) {
		KVP_LOG(LOG_ERR, "kvp_key_add_or_modify: returning 1\n");
		return (1);
	}

	/* Update the in-memory state. */
	kvp_update_mem_state(pool);

	num_records = kvp_pools[pool].num_records;
	record = kvp_pools[pool].records;
	num_blocks = kvp_pools[pool].num_blocks;

	for (i = 0; i < num_records; i++)
	{
		if (memcmp(key, record[i].key, key_size)) {
			continue;
		}

		/*
		 * Key exists. Just update the value and we are done.
		 */
		memcpy(record[i].value, value, value_size);
		kvp_update_file(pool);
		return (0);
	}

	/*
	 * Key doesn't exist; Add a new KVP.
	 */
	if (num_records == (ENTRIES_PER_BLOCK * num_blocks)) {
		/* Increase the size of the recodrd array. */
		record = realloc(record, sizeof(struct kvp_record) *
			ENTRIES_PER_BLOCK * (num_blocks + 1));

		if (record == NULL) {
			return (1);
		}
		kvp_pools[pool].num_blocks++;
	}
	memcpy(record[i].value, value, value_size);
	memcpy(record[i].key, key, key_size);
	kvp_pools[pool].records = record;
	kvp_pools[pool].num_records++;
	kvp_update_file(pool);
	return (0);
}


static int
kvp_get_value(int pool, __u8 *key, int key_size, __u8 *value,
    int value_size)
{
	int i;
	int num_records;
	struct kvp_record *record;

	KVP_LOG(LOG_DEBUG, "kvp_get_value: pool =  %d, key = %s\n,",
	    pool, key);

	if ((key_size > HV_KVP_EXCHANGE_MAX_KEY_SIZE) ||
	    (value_size > HV_KVP_EXCHANGE_MAX_VALUE_SIZE)) {
		return (1);
	}

	/* Update the in-memory state first. */
	kvp_update_mem_state(pool);

	num_records = kvp_pools[pool].num_records;
	record = kvp_pools[pool].records;

	for (i = 0; i < num_records; i++)
	{
		if (memcmp(key, record[i].key, key_size)) {
			continue;
		}

		/* Found the key */
		memcpy(value, record[i].value, value_size);
		return (0);
	}

	return (1);
}


static int
kvp_pool_enumerate(int pool, int idx, __u8 *key, int key_size,
    __u8 *value, int value_size)
{
	struct kvp_record *record;

	KVP_LOG(LOG_DEBUG, "kvp_pool_enumerate: pool = %d, index = %d\n,",
	    pool, idx);

	/* First update our in-memory state first. */
	kvp_update_mem_state(pool);
	record = kvp_pools[pool].records;

	/* Index starts with 0 */
	if (idx >= kvp_pools[pool].num_records) {
		return (1);
	}

	memcpy(key, record[idx].key, key_size);
	memcpy(value, record[idx].value, value_size);
	return (0);
}


static void
kvp_get_os_info(void)
{
	char *p;

	uname(&uts_buf);
	os_build = uts_buf.release;
	os_name = uts_buf.sysname;
	processor_arch = uts_buf.machine;

	/*
	 * Win7 host expects the build string to be of the form: x.y.z
	 * Strip additional information we may have.
	 */
	p = strchr(os_build, '-');
	if (p) {
		*p = '\0';
	}

	/*
	 * We don't have any other information about the FreeBSD os.
	 */
	return;
}

/*
 * Given the interface name, return the MAC address.
 */
static char *
kvp_if_name_to_mac(char *if_name)
{
	char *mac_addr = NULL;
	struct ifaddrs *ifaddrs_ptr;
	struct ifaddrs *head_ifaddrs_ptr;
	struct sockaddr_dl *sdl;
	int status;

	status = getifaddrs(&ifaddrs_ptr);

	if (status >= 0) {
		head_ifaddrs_ptr = ifaddrs_ptr;
		do {
			sdl = (struct sockaddr_dl *)(uintptr_t)ifaddrs_ptr->ifa_addr;
			if ((sdl->sdl_type == IFT_ETHER) &&
			    (strcmp(ifaddrs_ptr->ifa_name, if_name) == 0)) {
				mac_addr = strdup(ether_ntoa((struct ether_addr *)(LLADDR(sdl))));
				break;
			}
		} while ((ifaddrs_ptr = ifaddrs_ptr->ifa_next) != NULL);
		freeifaddrs(head_ifaddrs_ptr);
	}

	return (mac_addr);
}


/*
 * Given the MAC address, return the interface name.
 */
static char *
kvp_mac_to_if_name(char *mac)
{
	char *if_name = NULL;
	struct ifaddrs *ifaddrs_ptr;
	struct ifaddrs *head_ifaddrs_ptr;
	struct sockaddr_dl *sdl;
	int status;
	char *buf_ptr, *p;

	status = getifaddrs(&ifaddrs_ptr);

	if (status >= 0) {
		head_ifaddrs_ptr = ifaddrs_ptr;
		do {
			sdl = (struct sockaddr_dl *)(uintptr_t)ifaddrs_ptr->ifa_addr;
			if (sdl->sdl_type == IFT_ETHER) {
				buf_ptr = strdup(ether_ntoa((struct ether_addr *)(LLADDR(sdl))));
				if (buf_ptr != NULL) {
					for (p = buf_ptr; *p != '\0'; p++)
						*p = toupper(*p);

					if (strncmp(buf_ptr, mac, strlen(mac)) == 0) {
						/* Caller will free the memory */
						if_name = strdup(ifaddrs_ptr->ifa_name);
						free(buf_ptr);
						break;
					} else
						free(buf_ptr);
				}
			}
		} while ((ifaddrs_ptr = ifaddrs_ptr->ifa_next) != NULL);
		freeifaddrs(head_ifaddrs_ptr);
	}
	return (if_name);
}


static void
kvp_process_ipconfig_file(char *cmd,
    char *config_buf, size_t len,
    size_t element_size, int offset)
{
	char buf[256];
	char *p;
	char *x;
	FILE *file;

	/*
	 * First execute the command.
	 */
	file = popen(cmd, "r");
	if (file == NULL) {
		return;
	}

	if (offset == 0) {
		memset(config_buf, 0, len);
	}
	while ((p = fgets(buf, sizeof(buf), file)) != NULL) {
		if ((len - strlen(config_buf)) < (element_size + 1)) {
			break;
		}

		x = strchr(p, '\n');
		*x = '\0';
		strlcat(config_buf, p, len);
		strlcat(config_buf, ";", len);
	}
	pclose(file);
}


static void
kvp_get_ipconfig_info(char *if_name, struct hv_kvp_ipaddr_value *buffer)
{
	char cmd[512];
	char dhcp_info[128];
	char *p;
	FILE *file;

	/*
	 * Retrieve the IPV4 address of default gateway.
	 */
	snprintf(cmd, sizeof(cmd), "netstat -rn | grep %s | awk '/default/ {print $2 }'", if_name);

	/*
	 * Execute the command to gather gateway IPV4 info.
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->gate_way,
	    (MAX_GATEWAY_SIZE * 2), INET_ADDRSTRLEN, 0);
	/*
	 * Retrieve the IPV6 address of default gateway.
	 */
	snprintf(cmd, sizeof(cmd), "netstat -rn inet6 | grep %s | awk '/default/ {print $2 }'", if_name);

	/*
	 * Execute the command to gather gateway IPV6 info.
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->gate_way,
	    (MAX_GATEWAY_SIZE * 2), INET6_ADDRSTRLEN, 1);
	/*
	 * we just invoke an external script to get the DNS info.
	 *
	 * Following is the expected format of the information from the script:
	 *
	 * ipaddr1 (nameserver1)
	 * ipaddr2 (nameserver2)
	 * .
	 * .
	 */
	/* Scripts are stored in /usr/libexec/hyperv/ directory */
	snprintf(cmd, sizeof(cmd), "%s", "sh /usr/libexec/hyperv/hv_get_dns_info");

	/*
	 * Execute the command to get DNS info.
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->dns_addr,
	    (MAX_IP_ADDR_SIZE * 2), INET_ADDRSTRLEN, 0);

	/*
	 * Invoke an external script to get the DHCP state info.
	 * The parameter to the script is the interface name.
	 * Here is the expected output:
	 *
	 * Enabled: DHCP enabled.
	 */


	snprintf(cmd, sizeof(cmd), "%s %s",
	    "sh /usr/libexec/hyperv/hv_get_dhcp_info", if_name);

	file = popen(cmd, "r");
	if (file == NULL) {
		return;
	}

	p = fgets(dhcp_info, sizeof(dhcp_info), file);
	if (p == NULL) {
		pclose(file);
		return;
	}

	if (!strncmp(p, "Enabled", 7)) {
		buffer->dhcp_enabled = 1;
	} else{
		buffer->dhcp_enabled = 0;
	}

	pclose(file);
}


static unsigned int
hweight32(unsigned int *w)
{
	unsigned int res = *w - ((*w >> 1) & 0x55555555);

	res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
	res = (res + (res >> 4)) & 0x0F0F0F0F;
	res = res + (res >> 8);
	return ((res + (res >> 16)) & 0x000000FF);
}


static int
kvp_process_ip_address(void *addrp,
    int family, char *buffer,
    int length, int *offset)
{
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	int addr_length;
	char tmp[50];
	const char *str;

	if (family == AF_INET) {
		addr = (struct sockaddr_in *)addrp;
		str = inet_ntop(family, &addr->sin_addr, tmp, 50);
		addr_length = INET_ADDRSTRLEN;
	} else {
		addr6 = (struct sockaddr_in6 *)addrp;
		str = inet_ntop(family, &addr6->sin6_addr.s6_addr, tmp, 50);
		addr_length = INET6_ADDRSTRLEN;
	}

	if ((length - *offset) < addr_length + 1) {
		return (EINVAL);
	}
	if (str == NULL) {
		strlcpy(buffer, "inet_ntop failed\n", length);
		return (errno);
	}
	if (*offset == 0) {
		strlcpy(buffer, tmp, length);
	} else{
		strlcat(buffer, tmp, length);
	}
	strlcat(buffer, ";", length);

	*offset += strlen(str) + 1;
	return (0);
}


static int
kvp_get_ip_info(int family, char *if_name, int op,
    void *out_buffer, size_t length)
{
	struct ifaddrs *ifap;
	struct ifaddrs *curp;
	int offset = 0;
	int sn_offset = 0;
	int error = 0;
	char *buffer;
	size_t buffer_length;
	struct hv_kvp_ipaddr_value *ip_buffer = NULL;
	char cidr_mask[5];
	int weight;
	int i;
	unsigned int *w = NULL;
	char *sn_str;
	size_t sn_str_length;
	struct sockaddr_in6 *addr6;

	if (op == HV_KVP_OP_ENUMERATE) {
		buffer = out_buffer;
		buffer_length = length;
	} else {
		ip_buffer = out_buffer;
		buffer = (char *)ip_buffer->ip_addr;
		buffer_length = sizeof(ip_buffer->ip_addr);
		ip_buffer->addr_family = 0;
	}

	if (getifaddrs(&ifap)) {
		strlcpy(buffer, "getifaddrs failed\n", buffer_length);
		return (errno);
	}

	curp = ifap;
	while (curp != NULL) {
		if (curp->ifa_addr == NULL) {
			curp = curp->ifa_next;
			continue;
		}

		if ((if_name != NULL) &&
		    (strncmp(curp->ifa_name, if_name, strlen(if_name)))) {
			/*
			 * We want info about a specific interface;
			 * just continue.
			 */
			curp = curp->ifa_next;
			continue;
		}

		/*
		 * We support two address families: AF_INET and AF_INET6.
		 * If family value is 0, we gather both supported
		 * address families; if not we gather info on
		 * the specified address family.
		 */
		if ((family != 0) && (curp->ifa_addr->sa_family != family)) {
			curp = curp->ifa_next;
			continue;
		}
		if ((curp->ifa_addr->sa_family != AF_INET) &&
		    (curp->ifa_addr->sa_family != AF_INET6)) {
			curp = curp->ifa_next;
			continue;
		}

		if (op == HV_KVP_OP_GET_IP_INFO) {
			/*
			 * Get the info other than the IP address.
			 */
			if (curp->ifa_addr->sa_family == AF_INET) {
				ip_buffer->addr_family |= ADDR_FAMILY_IPV4;

				/*
				 * Get subnet info.
				 */
				error = kvp_process_ip_address(
					curp->ifa_netmask,
					AF_INET,
					(char *)
					ip_buffer->sub_net,
					length,
					&sn_offset);
				if (error) {
					goto kvp_get_ip_info_ipaddr;
				}
			} else {
				ip_buffer->addr_family |= ADDR_FAMILY_IPV6;

				/*
				 * Get subnet info in CIDR format.
				 */
				weight = 0;
				sn_str = (char *)ip_buffer->sub_net;
				sn_str_length = sizeof(ip_buffer->sub_net);
				addr6 = (struct sockaddr_in6 *)(uintptr_t)
				    curp->ifa_netmask;
				w = (unsigned int *)(uintptr_t)addr6->sin6_addr.s6_addr;

				for (i = 0; i < 4; i++)
				{
					weight += hweight32(&w[i]);
				}

				snprintf(cidr_mask, sizeof(cidr_mask), "/%d", weight);
				if ((length - sn_offset) <
				    (strlen(cidr_mask) + 1)) {
					goto kvp_get_ip_info_ipaddr;
				}

				if (sn_offset == 0) {
					strlcpy(sn_str, cidr_mask, sn_str_length);
				} else{
					strlcat(sn_str, cidr_mask, sn_str_length);
				}
				strlcat((char *)ip_buffer->sub_net, ";", sn_str_length);
				sn_offset += strlen(sn_str) + 1;
			}

			/*
			 * Collect other ip configuration info.
			 */
			kvp_get_ipconfig_info(if_name, ip_buffer);
		}

kvp_get_ip_info_ipaddr:
		error = kvp_process_ip_address(curp->ifa_addr,
			curp->ifa_addr->sa_family,
			buffer,
			length, &offset);
		if (error) {
			goto kvp_get_ip_info_done;
		}

		curp = curp->ifa_next;
	}

kvp_get_ip_info_done:
	freeifaddrs(ifap);
	return (error);
}


static int
kvp_write_file(FILE *f, const char *s1, const char *s2, const char *s3)
{
	int ret;

	ret = fprintf(f, "%s%s%s%s\n", s1, s2, "=", s3);

	if (ret < 0) {
		return (EIO);
	}

	return (0);
}


static int
kvp_set_ip_info(char *if_name, struct hv_kvp_ipaddr_value *new_val)
{
	int error = 0;
	char if_file[128];
	FILE *file;
	char cmd[512];
	char *mac_addr;

	/*
	 * FreeBSD - Configuration File
	 */
	snprintf(if_file, sizeof(if_file), "%s%s", "/var/db/hyperv",
	    "hv_set_ip_data");
	file = fopen(if_file, "w");

	if (file == NULL) {
		KVP_LOG(LOG_ERR, "FreeBSD Failed to open config file\n");
		return (errno);
	}

	/*
	 * Write out the MAC address.
	 */

	mac_addr = kvp_if_name_to_mac(if_name);
	if (mac_addr == NULL) {
		error = EINVAL;
		goto kvp_set_ip_info_error;
	}
	/* MAC Address */
	error = kvp_write_file(file, "HWADDR", "", mac_addr);
	if (error) {
		goto kvp_set_ip_info_error;
	}

	/* Interface Name  */
	error = kvp_write_file(file, "IF_NAME", "", if_name);
	if (error) {
		goto kvp_set_ip_info_error;
	}

	/* IP Address  */
	error = kvp_write_file(file, "IP_ADDR", "",
	    (char *)new_val->ip_addr);
	if (error) {
		goto kvp_set_ip_info_error;
	}

	/* Subnet Mask */
	error = kvp_write_file(file, "SUBNET", "",
	    (char *)new_val->sub_net);
	if (error) {
		goto kvp_set_ip_info_error;
	}


	/* Gateway */
	error = kvp_write_file(file, "GATEWAY", "",
	    (char *)new_val->gate_way);
	if (error) {
		goto kvp_set_ip_info_error;
	}

	/* DNS */
	error = kvp_write_file(file, "DNS", "", (char *)new_val->dns_addr);
	if (error) {
		goto kvp_set_ip_info_error;
	}

	/* DHCP */
	if (new_val->dhcp_enabled) {
		error = kvp_write_file(file, "DHCP", "", "1");
	} else{
		error = kvp_write_file(file, "DHCP", "", "0");
	}

	if (error) {
		goto kvp_set_ip_info_error;
	}

	free(mac_addr);
	fclose(file);

	/*
	 * Invoke the external script with the populated
	 * configuration file.
	 */

	snprintf(cmd, sizeof(cmd), "%s %s",
	    "sh /usr/libexec/hyperv/hv_set_ifconfig", if_file);
	system(cmd);
	return (0);

kvp_set_ip_info_error:
	KVP_LOG(LOG_ERR, "Failed to write config file\n");
	free(mac_addr);
	fclose(file);
	return (error);
}


static int
kvp_get_domain_name(char *buffer, int length)
{
	struct addrinfo hints, *info;
	int error = 0;

	gethostname(buffer, length);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;    /* Get only ipv4 addrinfo. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	error = getaddrinfo(buffer, NULL, &hints, &info);
	if (error != 0) {
		strlcpy(buffer, "getaddrinfo failed\n", length);
		return (error);
	}
	strlcpy(buffer, info->ai_canonname, length);
	freeaddrinfo(info);
	return (error);
}


static int
kvp_op_getipinfo(struct hv_kvp_msg *op_msg, void *data __unused)
{
	struct hv_kvp_ipaddr_value *ip_val;
	char *if_name;
	int error = 0;

	assert(op_msg != NULL);
	KVP_LOG(LOG_DEBUG, "In kvp_op_getipinfo.\n");

	ip_val = &op_msg->body.kvp_ip_val;
	op_msg->hdr.error = HV_S_OK;

	if_name = kvp_mac_to_if_name((char *)ip_val->adapter_id);

	if (if_name == NULL) {
		/* No interface found with the mac address. */
		op_msg->hdr.error = HV_E_FAIL;
		goto kvp_op_getipinfo_done;
	}

	error = kvp_get_ip_info(0, if_name,
	    HV_KVP_OP_GET_IP_INFO, ip_val, (MAX_IP_ADDR_SIZE * 2));
	if (error)
		op_msg->hdr.error = HV_E_FAIL;
	free(if_name);

kvp_op_getipinfo_done:
	return (error);
}


static int
kvp_op_setipinfo(struct hv_kvp_msg *op_msg, void *data __unused)
{
	struct hv_kvp_ipaddr_value *ip_val;
	char *if_name;
	int error = 0;

	assert(op_msg != NULL);
	KVP_LOG(LOG_DEBUG, "In kvp_op_setipinfo.\n");

	ip_val = &op_msg->body.kvp_ip_val;
	op_msg->hdr.error = HV_S_OK;

	if_name = (char *)ip_val->adapter_id;

	if (if_name == NULL) {
		/* No adapter provided. */
		op_msg->hdr.error = HV_GUID_NOTFOUND;
		goto kvp_op_setipinfo_done;
	}

	error = kvp_set_ip_info(if_name, ip_val);
	if (error)
		op_msg->hdr.error = HV_E_FAIL;
kvp_op_setipinfo_done:
	return (error);
}


static int
kvp_op_setgetdel(struct hv_kvp_msg *op_msg, void *data)
{
	struct kvp_op_hdlr *op_hdlr = (struct kvp_op_hdlr *)data;
	int error = 0;
	int op_pool;

	assert(op_msg != NULL);
	assert(op_hdlr != NULL);

	op_pool = op_msg->hdr.kvp_hdr.pool;
	op_msg->hdr.error = HV_S_OK;

	switch(op_hdlr->kvp_op_key) {
	case HV_KVP_OP_SET:
		if (op_pool == HV_KVP_POOL_AUTO) {
			/* Auto Pool is not writeable from host side. */
			error = 1;
			KVP_LOG(LOG_ERR, "Ilegal to write to pool %d from host\n",
			    op_pool);
		} else {
			error = kvp_key_add_or_modify(op_pool,
			    op_msg->body.kvp_set.data.key,
			    op_msg->body.kvp_set.data.key_size,
			    op_msg->body.kvp_set.data.msg_value.value,
			    op_msg->body.kvp_set.data.value_size);
		}
		break;

	case HV_KVP_OP_GET:
		error = kvp_get_value(op_pool,
		    op_msg->body.kvp_get.data.key,
		    op_msg->body.kvp_get.data.key_size,
		    op_msg->body.kvp_get.data.msg_value.value,
		    op_msg->body.kvp_get.data.value_size);
		break;

	case HV_KVP_OP_DELETE:
		if (op_pool == HV_KVP_POOL_AUTO) {
			/* Auto Pool is not writeable from host side. */
			error = 1;
			KVP_LOG(LOG_ERR, "Ilegal to change pool %d from host\n",
			    op_pool);
		} else {
			error = kvp_key_delete(op_pool,
			    op_msg->body.kvp_delete.key,
			    op_msg->body.kvp_delete.key_size);
		}
		break;

	default:
		break;
	}

	if (error != 0)
		op_msg->hdr.error = HV_S_CONT;
	return(error);
}


static int
kvp_op_enumerate(struct hv_kvp_msg *op_msg, void *data __unused)
{
	char *key_name, *key_value;
	int error = 0;
	int op_pool;
	int op;

	assert(op_msg != NULL);

	op = op_msg->hdr.kvp_hdr.operation;
	op_pool = op_msg->hdr.kvp_hdr.pool;
	op_msg->hdr.error = HV_S_OK;

	/*
	 * If the pool is not HV_KVP_POOL_AUTO, read from the appropriate
	 * pool and return the KVP according to the index requested.
	 */
	if (op_pool != HV_KVP_POOL_AUTO) {
		if (kvp_pool_enumerate(op_pool,
		    op_msg->body.kvp_enum_data.index,
		    op_msg->body.kvp_enum_data.data.key,
		    HV_KVP_EXCHANGE_MAX_KEY_SIZE,
		    op_msg->body.kvp_enum_data.data.msg_value.value,
		    HV_KVP_EXCHANGE_MAX_VALUE_SIZE)) {
			op_msg->hdr.error = HV_S_CONT;
			error = -1;
		}
		goto kvp_op_enumerate_done;
	}

	key_name = (char *)op_msg->body.kvp_enum_data.data.key;
	key_value = (char *)op_msg->body.kvp_enum_data.data.msg_value.value;

	switch (op_msg->body.kvp_enum_data.index)
	{
	case FullyQualifiedDomainName:
		kvp_get_domain_name(key_value,
		    HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "FullyQualifiedDomainName");
		break;

	case IntegrationServicesVersion:
		strcpy(key_name, "IntegrationServicesVersion");
		strlcpy(key_value, lic_version, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		break;

	case NetworkAddressIPv4:
		kvp_get_ip_info(AF_INET, NULL, HV_KVP_OP_ENUMERATE,
		    key_value, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "NetworkAddressIPv4");
		break;

	case NetworkAddressIPv6:
		kvp_get_ip_info(AF_INET6, NULL, HV_KVP_OP_ENUMERATE,
		    key_value, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "NetworkAddressIPv6");
		break;

	case OSBuildNumber:
		strlcpy(key_value, os_build, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "OSBuildNumber");
		break;

	case OSName:
		strlcpy(key_value, os_name, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "OSName");
		break;

	case OSMajorVersion:
		strlcpy(key_value, os_major, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "OSMajorVersion");
		break;

	case OSMinorVersion:
		strlcpy(key_value, os_minor, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "OSMinorVersion");
		break;

	case OSVersion:
		strlcpy(key_value, os_build, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "OSVersion");
		break;

	case ProcessorArchitecture:
		strlcpy(key_value, processor_arch, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
		strcpy(key_name, "ProcessorArchitecture");
		break;

	default:
#ifdef DEBUG
		KVP_LOG(LOG_ERR, "Auto pool Index %d not found.\n",
		    op_msg->body.kvp_enum_data.index);
#endif
		op_msg->hdr.error = HV_S_CONT;
		error = -1;
		break;
	}

kvp_op_enumerate_done:
	if (error != 0)
		op_msg->hdr.error = HV_S_CONT;
	return(error);
}


/*
 * Load handler, and call init routine if provided.
 */
static int
kvp_op_load(int key, void (*init)(void),
	    int (*exec)(struct hv_kvp_msg *, void *))
{
	int error = 0;

	if (key < 0 || key >= HV_KVP_OP_COUNT) {
		KVP_LOG(LOG_ERR, "Operation key out of supported range\n");
		error = -1;
		goto kvp_op_load_done;
	}

	kvp_op_hdlrs[key].kvp_op_key = key;
	kvp_op_hdlrs[key].kvp_op_init = init;
	kvp_op_hdlrs[key].kvp_op_exec = exec;

	if (kvp_op_hdlrs[key].kvp_op_init != NULL)
		kvp_op_hdlrs[key].kvp_op_init();

kvp_op_load_done:
	return(error);
}


/*
 * Initialize the operation hanlders.
 */
static int
kvp_ops_init(void)
{
	int i;

	/* Set the initial values. */
	for (i = 0; i < HV_KVP_OP_COUNT; i++) {
		kvp_op_hdlrs[i].kvp_op_key = -1;
		kvp_op_hdlrs[i].kvp_op_init = NULL;
		kvp_op_hdlrs[i].kvp_op_exec = NULL;
	}

	return(kvp_op_load(HV_KVP_OP_GET, NULL, kvp_op_setgetdel) |
	    kvp_op_load(HV_KVP_OP_SET, NULL, kvp_op_setgetdel) |
	    kvp_op_load(HV_KVP_OP_DELETE, NULL, kvp_op_setgetdel) |
	    kvp_op_load(HV_KVP_OP_ENUMERATE, kvp_get_os_info,
	        kvp_op_enumerate) |
	    kvp_op_load(HV_KVP_OP_GET_IP_INFO, NULL, kvp_op_getipinfo) |
	    kvp_op_load(HV_KVP_OP_SET_IP_INFO, NULL, kvp_op_setipinfo));
}


int
main(int argc, char *argv[])
{
	struct hv_kvp_msg *hv_kvp_dev_buf;
	struct hv_kvp_msg *hv_msg;
	struct pollfd hv_kvp_poll_fd[1];
	int op, pool;
	int hv_kvp_dev_fd, error, len, r;
	int ch;

	while ((ch = getopt(argc, argv, "dn")) != -1) {
		switch (ch) {
		case 'n':
			/* Run as regular process for debugging purpose. */
			is_daemon = 0;
			break;
		case 'd':
			/* Generate debugging output */
			is_debugging = 1;
			break;
		default:
			break;
		}
	}

	openlog("HV_KVP", 0, LOG_USER);

	/* Become daemon first. */
	if (is_daemon == 1)
		daemon(1, 0);
	else
		KVP_LOG(LOG_DEBUG, "Run as regular process.\n");

	KVP_LOG(LOG_INFO, "HV_KVP starting; pid is: %d\n", getpid());

	/* Communication buffer hv_kvp_dev_buf */
	hv_kvp_dev_buf = malloc(sizeof(*hv_kvp_dev_buf));
	/* Buffer for daemon internal use */
	hv_msg = malloc(sizeof(*hv_msg));

	/* Memory allocation failed */
	if (hv_kvp_dev_buf == NULL || hv_msg == NULL) {
		KVP_LOG(LOG_ERR, "Failed to allocate memory for hv buffer\n");
		exit(EXIT_FAILURE);
	}

	/* Initialize op handlers */
	if (kvp_ops_init() != 0) {
		KVP_LOG(LOG_ERR, "Failed to initizlize operation handlers\n");
		exit(EXIT_FAILURE);
	}

	if (kvp_file_init()) {
		KVP_LOG(LOG_ERR, "Failed to initialize the pools\n");
		exit(EXIT_FAILURE);
	}

	/* Open the Character Device */
	hv_kvp_dev_fd = open("/dev/hv_kvp_dev", O_RDWR);

	if (hv_kvp_dev_fd < 0) {
		KVP_LOG(LOG_ERR, "open /dev/hv_kvp_dev failed; error: %d %s\n",
		    errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Initialize the struct for polling the char device */
	hv_kvp_poll_fd[0].fd = hv_kvp_dev_fd;
	hv_kvp_poll_fd[0].events = (POLLIN | POLLRDNORM);

	/* Register the daemon to the KVP driver */
	memset(hv_kvp_dev_buf, 0, sizeof(*hv_kvp_dev_buf));
	hv_kvp_dev_buf->hdr.kvp_hdr.operation = HV_KVP_OP_REGISTER;
	len = write(hv_kvp_dev_fd, hv_kvp_dev_buf, sizeof(*hv_kvp_dev_buf));


	for (;;) {
		r = poll (hv_kvp_poll_fd, 1, INFTIM);

		KVP_LOG(LOG_DEBUG, "poll returned r = %d, revent = 0x%x\n",
		    r, hv_kvp_poll_fd[0].revents);

		if (r == 0 || (r < 0 && errno == EAGAIN) ||
		    (r < 0 && errno == EINTR)) {
			/* Nothing to read */
			continue;
		}

		if (r < 0) {
			/*
			 * For pread return failure other than EAGAIN,
			 * we want to exit.
			 */
			KVP_LOG(LOG_ERR, "Poll failed.\n");
			perror("poll");
			exit(EIO);
		}

		/* Read from character device */
		len = pread(hv_kvp_dev_fd, hv_kvp_dev_buf,
		    sizeof(*hv_kvp_dev_buf), 0);

		if (len < 0) {
			KVP_LOG(LOG_ERR, "Read failed.\n");
			perror("pread");
			exit(EIO);
		}

		if (len != sizeof(struct hv_kvp_msg)) {
			KVP_LOG(LOG_ERR, "read len is: %d\n", len);
			continue;
		}

		/* Copy hv_kvp_dev_buf to hv_msg */
		memcpy(hv_msg, hv_kvp_dev_buf, sizeof(*hv_msg));

		/*
		 * We will use the KVP header information to pass back
		 * the error from this daemon. So, first save the op
		 * and pool info to local variables.
		 */

		op = hv_msg->hdr.kvp_hdr.operation;
		pool = hv_msg->hdr.kvp_hdr.pool;

		if (op < 0 || op >= HV_KVP_OP_COUNT ||
		    kvp_op_hdlrs[op].kvp_op_exec == NULL) {
			KVP_LOG(LOG_WARNING,
			    "Unsupported operation OP = %d\n", op);
			hv_msg->hdr.error = HV_ERROR_NOT_SUPPORTED;
		} else {
			/*
			 * Call the operateion handler's execution routine.
			 */
			error = kvp_op_hdlrs[op].kvp_op_exec(hv_msg,
			    (void *)&kvp_op_hdlrs[op]);
			if (error != 0) {
				assert(hv_msg->hdr.error != HV_S_OK);
				if (hv_msg->hdr.error != HV_S_CONT)
					KVP_LOG(LOG_WARNING,
					    "Operation failed OP = %d, error = 0x%x\n",
					    op, error);
			}
		}

		/*
		 * Send the value back to the kernel. The response is
		 * already in the receive buffer.
		 */
hv_kvp_done:
		len = pwrite(hv_kvp_dev_fd, hv_msg, sizeof(*hv_kvp_dev_buf), 0);

		if (len != sizeof(struct hv_kvp_msg)) {
			KVP_LOG(LOG_ERR, "write len is: %d\n", len);
			goto hv_kvp_done;
		}
	}
}
