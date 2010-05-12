/*
 * Copyright (c) 2010 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_NVEC_DEVICE_H
#define INCLUDED_NVEC_DEVICE_H


#if defined(__cplusplus)
extern "C"
{
#endif

#define nvec_get_drvdata(f)     dev_get_drvdata(&(f)->dev)
#define nvec_set_drvdata(f,d)   dev_set_drvdata(&(f)->dev, d)

struct nvec_driver;

struct nvec_device {
	char *name;
	struct device		*parent;
	struct device dev;
	struct bus_type	*bus;		/* type of bus device is on */
	struct nvec_driver *driver;	/* which driver has allocated this
					   device */
};

extern int nvec_register_device(struct nvec_device *pdev);
extern void nvec_unregister_device(struct nvec_device *pdev);

/*
 * NVEC function device driver
 */
struct nvec_driver {
	char *name;
	struct device_driver driver;
	struct device dev;

	int (*probe)(struct nvec_device *);
	void (*remove)(struct nvec_device *);

	int (*suspend)(struct nvec_device *dev, pm_message_t state);
	int (*resume)(struct nvec_device *dev);
};

extern int nvec_register_driver(struct nvec_driver *);
extern void nvec_unregister_driver(struct nvec_driver *);

#if defined(__cplusplus)
}
#endif

#endif // INCLUDED_NVEC_DEVICE_H
