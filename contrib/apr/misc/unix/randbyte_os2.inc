/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* The high resolution timer API provides access to the hardware timer 
 * running at around 1.1MHz. The amount this changes in a time slice is
 * varies randomly due to system events, hardware interrupts etc
 */
static UCHAR randbyte_hrtimer()
{
    QWORD t1, t2;
    UCHAR byte;

    DosTmrQueryTime(&t1);
    DosSleep(5);
    DosTmrQueryTime(&t2);

    byte = (t2.ulLo - t1.ulLo) & 0xFF;
    byte ^= (t2.ulLo - t1.ulLo) >> 8;
    return byte;
}



/* A bunch of system information like memory & process stats.
 * Not highly random but every bit helps....
 */
static UCHAR randbyte_sysinfo()
{
    UCHAR byte = 0;
    UCHAR SysVars[100];
    int b;

    DosQuerySysInfo(1, QSV_FOREGROUND_PROCESS, SysVars, sizeof(SysVars));

    for (b = 0; b < 100; b++) {
        byte ^= SysVars[b];
    }

    return byte;
}



/* Similar in concept to randbyte_hrtimer() but accesses the CPU's internal
 * counters which run at the CPU's MHz speed. We get separate 
 * idle / busy / interrupt cycle counts which should provide very good 
 * randomness due to interference of hardware events.
 * This only works on newer CPUs (at least PPro or K6) and newer OS/2 versions
 * which is why it's run-time linked.
 */

static APIRET APIENTRY(*DosPerfSysCall) (ULONG ulCommand, ULONG ulParm1,
                                         ULONG ulParm2, ULONG ulParm3) = NULL;
static HMODULE hDoscalls = 0;
#define   CMD_KI_RDCNT    (0x63)

typedef struct _CPUUTIL {
    ULONG ulTimeLow;            /* Low 32 bits of time stamp      */
    ULONG ulTimeHigh;           /* High 32 bits of time stamp     */
    ULONG ulIdleLow;            /* Low 32 bits of idle time       */
    ULONG ulIdleHigh;           /* High 32 bits of idle time      */
    ULONG ulBusyLow;            /* Low 32 bits of busy time       */
    ULONG ulBusyHigh;           /* High 32 bits of busy time      */
    ULONG ulIntrLow;            /* Low 32 bits of interrupt time  */
    ULONG ulIntrHigh;           /* High 32 bits of interrupt time */
} CPUUTIL;


static UCHAR randbyte_perf()
{
    UCHAR byte = 0;
    CPUUTIL util;
    int c;

    if (hDoscalls == 0) {
        char failed_module[20];
        ULONG rc;

        rc = DosLoadModule(failed_module, sizeof(failed_module), "DOSCALLS",
                           &hDoscalls);

        if (rc == 0) {
            rc = DosQueryProcAddr(hDoscalls, 976, NULL, (PFN *)&DosPerfSysCall);

            if (rc) {
                DosPerfSysCall = NULL;
            }
        }
    }

    if (DosPerfSysCall) {
        if (DosPerfSysCall(CMD_KI_RDCNT, (ULONG)&util, 0, 0) == 0) {
            for (c = 0; c < sizeof(util); c++) {
                byte ^= ((UCHAR *)&util)[c];
            }
        }
        else {
            DosPerfSysCall = NULL;
        }
    }

    return byte;
}



static UCHAR randbyte()
{
    return randbyte_hrtimer() ^ randbyte_sysinfo() ^ randbyte_perf();
}
