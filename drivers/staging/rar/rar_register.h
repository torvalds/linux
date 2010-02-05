/* === RAR Physical Addresses === */
struct RAR_address_struct {
	u32 low;
	u32 high;
};

/* The get_rar_address function is used by other device drivers
 * to obtain RAR address information on a RAR. It takes two
 * parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar for which you wish to retrieve
 * the address information.
 * Values can be 0,1, or 2.
 *
 * struct RAR_address_struct is a pointer to a place to which the function
 * can return the address structure for the RAR.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int get_rar_address(int rar_index, struct RAR_address_struct *addresses);


/* The lock_rar function is used by other device drivers to lock an RAR.
 * once an RAR is locked, it stays locked until the next system reboot.
 * The function takes one parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar that you want to lock.
 * Values can be 0,1, or 2.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int lock_rar(int rar_index);


/* DEBUG LEVEL MASKS */
#define RAR_DEBUG_LEVEL_BASIC       0x1

#define RAR_DEBUG_LEVEL_REGISTERS   0x2

#define RAR_DEBUG_LEVEL_EXTENDED    0x4

#define DEBUG_LEVEL	0x7

/* FUNCTIONAL MACROS */

/* debug macro without paramaters */
#define DEBUG_PRINT_0(DEBUG_LEVEL , info) \
do { \
	if (DEBUG_LEVEL) { \
		printk(KERN_WARNING info); \
	} \
} while (0)

/* debug macro with 1 paramater */
#define DEBUG_PRINT_1(DEBUG_LEVEL , info , param1) \
do { \
	if (DEBUG_LEVEL) { \
		printk(KERN_WARNING info , param1); \
	} \
} while (0)

/* debug macro with 2 paramaters */
#define DEBUG_PRINT_2(DEBUG_LEVEL , info , param1, param2) \
do { \
	if (DEBUG_LEVEL) { \
		printk(KERN_WARNING info , param1, param2); \
	} \
} while (0)

/* debug macro with 3 paramaters */
#define DEBUG_PRINT_3(DEBUG_LEVEL , info , param1, param2 , param3) \
do { \
	if (DEBUG_LEVEL) { \
		printk(KERN_WARNING info , param1, param2 , param3); \
	} \
} while (0)

/* debug macro with 4 paramaters */
#define DEBUG_PRINT_4(DEBUG_LEVEL , info , param1, param2 , param3 , param4) \
do { \
	if (DEBUG_LEVEL) { \
		printk(KERN_WARNING info , param1, param2 , param3 , param4); \
	} \
} while (0)

