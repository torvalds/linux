//- Copyright (c) 2010 James Grenning and Contributed to Unity Project
/* ==========================================
    Unity Project - A Test Framework for C
    Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
    [Released under MIT License. Please refer to license.txt for details]
========================================== */

#ifndef UNITY_FIXTURE_INTERNALS_H_
#define UNITY_FIXTURE_INTERNALS_H_

typedef struct _UNITY_FIXTURE_T
{
    int Verbose;
    unsigned int RepeatCount;
    const char* NameFilter;
    const char* GroupFilter;
} UNITY_FIXTURE_T;

typedef void unityfunction(void);
void UnityTestRunner(unityfunction * setup,
        unityfunction * body,
        unityfunction * teardown,
        const char * printableName,
        const char * group,
        const char * name,
        const char * file, int line);

void UnityIgnoreTest(const char * printableName);
void UnityMalloc_StartTest(void);
void UnityMalloc_EndTest(void);
int UnityFailureCount(void);
int UnityGetCommandLineOptions(int argc, const char* argv[]);
void UnityConcludeFixtureTest(void);

void UnityPointer_Set(void ** ptr, void * newValue);
void UnityPointer_UndoAllSets(void);
void UnityPointer_Init(void);

void UnityAssertEqualPointer(const void * expected,
                            const void * actual,
                            const char* msg,
                            const UNITY_LINE_TYPE lineNumber);

#endif /* UNITY_FIXTURE_INTERNALS_H_ */
