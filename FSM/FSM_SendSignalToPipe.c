#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSM_Client/FSM_client.h"

pid_t FSM_SSTP_PID = 0;
struct siginfo FSM_SSTP_info;
struct task_struct* FSM_SSTP_task;
struct SendSignalStruct FSM_SSTP_signstr;
void FSM_EventIOCtl(char* Data, short len, struct fsm_ioctl_struct* ioctl)
{
    struct FSM_SendCmdUserspace* fsm_scus = (struct FSM_SendCmdUserspace*)Data;
    switch(fsm_scus->cmd) {
    case FSM_SSTP_SetPid:
        FSM_SSTP_info.si_signo = SIGUSR1;
        FSM_SSTP_info.si_errno = 0;
        FSM_SSTP_info.si_code = SI_USER;
        FSM_SSTP_PID = ((pid_t*)fsm_scus->Data)[0];
        printk(KERN_INFO "FSM Set Setting %u, ", FSM_SSTP_PID);
        // task = pid_task(find_pid_ns(FSM_SSTP_PID, &init_pid_ns), PIDTYPE_PID);
        // task = pid_task(find_vpid(FSM_SSTP_PID),PIDTYPE_PID);
        FSM_SSTP_task = pid_task(find_vpid(FSM_SSTP_PID), PIDTYPE_PID);
        break;
    case FSM_SSTP_GetEvent:
        memcpy(fsm_scus->Data, &FSM_SSTP_signstr, sizeof(struct SendSignalStruct));
        printk(KERN_INFO "GEvent");
        break;
    }
}
int FSM_SendSignalToPipe_thread(void* Data)
{
    char id[10];
    struct SendSignalStruct* datas = (struct SendSignalStruct*)Data;
    char* envp[] = { NULL };
    char* argv[4];
    sprintf(id, "%u", datas->id);
    argv[0] = "fsmsstd";
    argv[1] = datas->pipe;
    argv[2] = id;
    argv[3] = NULL;

    call_usermodehelper("/bin/fsmsstd", argv, envp, UMH_WAIT_EXEC);
    return 0;
}

int FSM_SendSignalToPipe(char* pipe, int signal)
{

    FSM_SSTP_signstr.id = signal;
    strcpy(FSM_SSTP_signstr.pipe, pipe);
    if(FSM_SSTP_PID)
        send_sig_info(SIGUSR1, &FSM_SSTP_info, FSM_SSTP_task);
    // send_sig_info( SIGUSR1, FSM_SSTP_PID, 0 );
    // pid = kthread_run(FSM_SendSignalToPipe_thread, &signstr, "FSM_SendSignalToPipe" ); /* запускаем новый поток */
    // FSM_SendSignalToPipe_thread(&signstr);
    return 0;
}
EXPORT_SYMBOL(FSM_SendSignalToPipe);

static int __init FSMSendSigTP_init(void)
{
    FSM_RegisterIOCtl(FSM_EventIOCtlId, FSM_EventIOCtl);
    call_usermodehelper("/bin/fsmced", 0, 0, UMH_WAIT_EXEC);
    FSM_SendSignalToPipe("fsmstat", 2);
    printk(KERN_INFO "FSM Send Signal To Pipe loaded\n");

    return 0;
}
static void __exit FSMSendSigTP_exit(void)
{
    FSM_DeleteIOCtl(FSM_EventIOCtlId);
    printk(KERN_INFO "FSM Send Signal To Pipe unloaded\n");
}

module_init(FSMSendSigTP_init);
module_exit(FSMSendSigTP_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Send Signal To Pipe Module");
MODULE_LICENSE("GPL");