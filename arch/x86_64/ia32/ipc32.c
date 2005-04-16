#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/ipc.h>
#include <linux/compat.h>

#include <asm-i386/ipc.h>

asmlinkage long
sys32_ipc(u32 call, int first, int second, int third,
		compat_uptr_t ptr, u32 fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	      case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		return sys_semtimedop(first, compat_ptr(ptr), second, NULL);
	      case SEMTIMEDOP:
		return compat_sys_semtimedop(first, compat_ptr(ptr), second,
						compat_ptr(fifth));
	      case SEMGET:
		return sys_semget(first, second, third);
	      case SEMCTL:
		return compat_sys_semctl(first, second, third, compat_ptr(ptr));

	      case MSGSND:
		return compat_sys_msgsnd(first, second, third, compat_ptr(ptr));
	      case MSGRCV:
		return compat_sys_msgrcv(first, second, fifth, third,
					 version, compat_ptr(ptr));
	      case MSGGET:
		return sys_msgget((key_t) first, second);
	      case MSGCTL:
		return compat_sys_msgctl(first, second, compat_ptr(ptr));

	      case SHMAT:
		return compat_sys_shmat(first, second, third, version,
					compat_ptr(ptr));
		break;
	      case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	      case SHMGET:
		return sys_shmget(first, (unsigned)second, third);
	      case SHMCTL:
		return compat_sys_shmctl(first, second, compat_ptr(ptr));
	}
	return -ENOSYS;
}
