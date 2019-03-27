/*-
 * Copyright (c) 2011, 2012, 2013 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

/**
 * \file callout.h
 *
 * \brief Interface for timer based callback services.
 *
 * Header requirements:
 *
 *     #include <sys/time.h>
 *
 *     #include <list>
 */

#ifndef _CALLOUT_H_
#define _CALLOUT_H_

/**
 * \brief Type of the function callback from a Callout.
 */
typedef void CalloutFunc_t(void *);

/**
 * \brief Interface to a schedulable one-shot timer with the granularity
 *        of the system clock (see setitimer(2)).
 *
 * Determination of callback expiration is triggered by the SIGALRM
 * signal.  Callout callbacks are always delivered from Zfsd's event
 * processing loop.
 *
 * Periodic actions can be triggered via the Callout mechanisms by
 * resetting the Callout from within its callback.
 */
class Callout
{
public:

	/**
	 * Initialize the Callout subsystem.
	 */
	static void Init();

	/**
	 * Function called (via SIGALRM) when our interval
	 * timer expires.
	 */
	static void AlarmSignalHandler(int);

	/**
	 * Execute callbacks for all callouts that have the same
	 * expiration time as the first callout in the list.
	 */
	static void ExpireCallouts();

	/** Constructor. */
	Callout();

	/**
	 * Returns true if callout has not been stopped,
	 * or deactivated since the last time the callout was
	 * reset.
	 */
	bool IsActive() const;

	/**
	 * Returns true if callout is still waiting to expire.
	 */
	bool IsPending() const;

	/**
	 * Disestablish a callout.
	 */
	bool Stop();

	/**
	 * \brief Establish or change a timeout.
	 *
	 * \param interval  Timeval indicating the time which must elapse
	 *                  before this callout fires.
	 * \param func      Pointer to the callback function
	 * \param arg       Argument pointer to pass to callback function
	 *
	 * \return  Cancellation status.
	 *             true:  The previous callback was pending and therefore
	 *                    was cancelled.
	 *             false: The callout was not pending at the time of this
	 *                    reset request.
	 *          In all cases, a new callout is established.
	 */
	bool  Reset(const timeval &interval, CalloutFunc_t *func, void *arg);

	/**
	 * \brief Calculate the remaining time until this Callout's timer
	 *        expires.
	 *
	 * The return value will be slightly greater than the actual time to
	 * expiry.
	 *
	 * If the callout is not pending, returns INT_MAX.
	 */
	timeval TimeRemaining() const;

private:
	/**
	 * All active callouts sorted by expiration time.  The callout
	 * with the nearest expiration time is at the head of the list.
	 */
	static std::list<Callout *> s_activeCallouts;

	/**
	 * The interval timer has expired.  This variable is set from
	 * signal handler context and tested from Zfsd::EventLoop()
	 * context via ExpireCallouts().
	 */
	static bool                 s_alarmFired;

	/**
	 * Time, relative to others in the active list, until
	 * this callout is fired.
	 */
	timeval                     m_interval;

	/** Callback function argument. */
	void                       *m_arg;

	/**
	 * The callback function associated with this timer
	 * entry.
	 */
	CalloutFunc_t              *m_func;

	/** State of this callout. */
	bool                        m_pending;
};

//- Callout public const methods ----------------------------------------------
inline bool
Callout::IsPending() const
{
	return (m_pending);
}

//- Callout public methods ----------------------------------------------------
inline
Callout::Callout()
 : m_arg(0),
   m_func(NULL),
   m_pending(false)
{
	timerclear(&m_interval);
}

#endif /* CALLOUT_H_ */
