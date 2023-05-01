=====================================================
Assignment 7 - SO2 Virtual Machine Manager with KVM
=====================================================

- Deadline: :command:`Tuesday, 29 May 2023, 23:00`
- This assignment can be made in teams (max 2). Only one of them must submit the assignment, and the names of the student should be listed in a README file.

In this assignment we will work on a simple Virtual Machine Manager (VMM). We will be using the KVM API
from the Linux kernel. 

The assignment has two components: the VM code and the VMM code. We will be using a very simple protocol
to enable the communication between the two components. The protocol is called SIMVIRTIO.


I. Virtual Machine Manager
==========================

In general, to build a VMM from scratch we will have to implement three main functionalities: initialize the VMM, initialize the virtual CPU and run the guest code. We will split the implementation of the VMM in these three phases.

1. Initialize the VMM
-------------------------

A VM will be represented in general by three elements, a file descriptor used to interact with the KVM API, a file descriptor per VM used to configure it (e.g. set its memory) and a pointer to the VM's memory. We provide you with the following structure to start from when working with a VM.

.. code-block:: c

	typedef struct vm {
		int sys_fd;
		int fd;
		char *mem;
	} virtual_machine;


The first step in initializing the KVM VM is to interract with the [KVM_API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html]. The KVM API is exposed via ``/dev/kvm``. We will be using ioctl calls to call the API. 

The snippet below shows how one can call ``KVM_GET_API_VERSION`` to get the KVM API Version

.. code-block:: c

	int kvm_fd = open("/dev/kvm", O_RDWR);
	if (kvm_fd < 0) {
	    perror("open /dev/kvm");
	    exit(1);
	}

	int api_ver = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
	if (api_ver < 0) {
	    perror("KVM_GET_API_VERSION");
	    exit(1);
	}

Let us now go briefly through how a VMM initializes a VM. This is only the bare bones, a VMM may do lots of other things during VM initialization.

1. We first use KVM_GET_API_VERSION to check that we are running the expected version of KVM, ``KVM_API_VERSION``. 
2. We now create the VM using ``KVM_CREATE_VM``. Note that calling ``KVM_CREATE_VM`` returns a file descriptor. We will be using this file descriptor for the next phases of the setup. 
3. (Optional) On Intel based CPUs we will have to call ``KVM_SET_TSS_ADDR`` with address ``0xfffbd000``
4. Next, we allocate the memory for the VM, we will be using ``mmap`` for this with ``PROT_WRITE``, ``MAP_PRIVATE``, ``MAP_ANONYMOUS`` and ``MAP_NORESERVE``. We recommend allocating 0x100000 bytes for the VM.
5. We flag the memory as ``MADV_MERGEABLE`` using ``madvise``
6. Finally, we use ``KVM_SET_USER_MEMORY_REGION`` to assign the memory to the VM.

**Make sure you understand what file descriptor to use and when, we use the KVM fd when calling KVM_CREATE_VM, but when interacting with the vm such as calling KVM_SET_USER_MEMORY_REGION we use the VMs
file descriptor** 

TLDR: API used for VM initialization:

* KVM_GET_API_VERSION
* KVM_CREATE_VM
* KVM_SET_TSS_ADDR
* KVM_SET_USER_MEMORY_REGION.

2. Initialize a virtual CPU
___________________________

We need a Virtual CPU (VCPU) to store registers.

.. code-block:: c

	typedef struct vcpu {
		int fd;
		struct kvm_run *kvm_run;
	} virtual_cpu;

To create a virtual CPU we will do the following:
1. Call ``KVM_CREATE_VCPU`` to create the virtual CPU. This call returns a file descriptor.
2. Use ``KVM_GET_VCPU_MMAP_SIZE`` to get the size of the shared memory
3. Allocated the necessary VCPU mem size with ``mmap``. We will be passing the VCPU file descriptor to the ``mmap`` call. We can store the result in ``kvm_run``.


TLDR: API used for VM

* KVM_CREATE_VCPU
* KVM_GET_VCPU_MMAP_SIZE

**We recommend using 2MB pages to simplify the translation process**

Running the VM
==============


Setup real mode
---------------

At first, the CPU will start in Protected mode. To do run any meaningful code, we will switch the CPU to [Real mode](https://wiki.osdev.org/Real_Mode). To do this we will
need to configure several CPU registers.

1. First, we will use ``KVM_GET_SREGS`` to get the registers. We use ``struct kvm_regs`` for this task.
2. We will need to set ``cs.selector`` and ``cs.base`` to 0. We will use ``KVM_SET_SREGS`` to set the registers.
3. Next we will clear all ``FLAGS`` bits via the ``rflags`` register, this means setting ``rflags`` to 2 since bit 1 must always be to 1. We alo set the ``RIP`` register to 0.

Setup long mode
---------------

Read mode is all right for very simple guests, such as the one found in the folder `guest_16_bits`. But,
most programs nowdays need 64 bits addresses, and such we will need to switch to long mode. The following article from OSDev presents all the necessary information about  [Setting Up Long Mode](https://wiki.osdev.org/Setting_Up_Long_Mode).

In ``vcpu.h``, you may found helpful macros such as CR0_PE, CR0_MP, CR0_ET, etc. 

Since we will running a more complex program, we will also create a small stack for our program
``regs.rsp = 1 << 20;``. Don't forget to set the RIP and RFLAGS registers.

Running
-------

After we setup our VCPU in real or long mode we can finally start running code on the VM.

1. We copy to the vm memory the guest code, `memcpy(vm->mem, guest_code, guest_code_size)` The guest code will be available in two variables which will be discussed below.
2. In a infinite loop we run the following:
  * We call ``KVM_RUN`` on the VCPU file descriptor to run the VPCU
  * Through the shared memory of the VCPU we check the ``exit_reason`` parameter to see if the guest has made any requests:
  * We will handle the following VMEXITs: `KVM_EXIT_MMIO`, `KVM_EXIT_IO` and ``KVM_EXIT_HLT``. ``KVM_EXIT_MMIO`` is triggered when the VM writes to a MMIO address. ``KVM_EXIT_IO`` is called when the VM calls ``inb`` or ``outb``. ``KVM_EXIT_HLT`` is called when the user does a ``hlt`` instruction.

Guest code
----------

The VM that is running is also called guest. We will be using the guest to test our implementation.

1. To test the implementation before implementing SIMVIRTIO. The guest will write at address 400 and the RAX register the value 42.
2. To test a more complicated implementation,we will extend the previous program to also write "Hello, world!\n" on port `0xE9` using the `outb` instruction.
3. To test the implementation of `SIMVIRTIO`, we will 

How do we get the guest code? The guest code is available at the following static pointers guest16, guest16_end-guest16. The linker script is populating them.


## SIMVIRTIO:
From the communication between the guest and the VMM we will implement a very simple protocol called ``SIMVIRTIO``. It's a simplified version of the real protocol used in the real world called virtio.

Configuration space:

+--------------+----------------+----------------+----------------+------------------+-------------+-------------+
| u32          | u16            | u8             | u8             | u8               | u8          | u8          |
+==============+================+================+================+==================+=============+=============+
| magic value  | max queue len  | device status  | driver status  | queue selector   | Q0(TX) CTL  | Q1(RX) CTL  |
| R            | R              | R              | R/W            | R/W              | R/W         | R/w         |
+--------------+----------------+----------------+----------------+------------------+-------------+-------------+


Controller queues
-----------------

We provide you with the following structures and methods for the ``SIMVIRTIO`` implementation.

.. code-block:: c

	typedef uint8_t q_elem_t;
	typedef struct queue_control {
	    // Ptr to current available head/producer index in 'buffer'.
	    unsigned head;
	    // Ptr to last index in 'buffer' used by consumer.
	    unsigned tail;
	} queue_control_t;
	typedef struct simqueue {
	    // MMIO queue control.
	    volatile queue_control_t *q_ctrl;
	    // Size of the queue buffer/data.
	    unsigned maxlen;
	    // Queue data buffer.
	    q_elem_t *buffer;
	} simqueue_t;
	int circ_bbuf_push(simqueue_t *q, q_elem_t data)
	{
	}
	int circ_bbuf_pop(simqueue_t *q, q_elem_t *data)
	{
	}


Device structures
-----------------

.. code-block:: c

	#define MAGIC_VALUE 0x74726976
	#define DEVICE_RESET 0x0
	#define DEVICE_CONFIG 0x2
	#define DEVICE_READY 0x4
	#define DRIVER_ACK 0x0
	#define DRIVER 0x2
	#define DRIVER_OK 0x4
	#define DRIVER_RESET 0x8000
	typedef struct device {
	    uint32_t magic;
	    uint8_t device_status;
	    uint8_t driver_status;
	    uint8_t max_queue_len;
	} device_t;
	typedef struct device_table {
	    uint16_t count;
	    uint64_t device_addresses[10];
	 } device_table_t;
 

We will be implementing the following handles:
* MMIO (read/write) VMEXIT
* PIO (read/write) VMEXIT

Using the skeleton
==================

Debugging
=========


Tasks
=====
1. 30p Implement a simple VMM that runs the code from `guest_16_bits`. We will be running the VCPU in read mode for this task
2. 20p Extend the previous implementation to run the VCPU in real mode. We will be running the `guest_32_bits` example
3. 30p Implement the `SIMVIRTIO` protocol.
4. 10p Implement pooling as opposed to VMEXIT. We will use the macro `USE_POOLING` to switch this option on and off.
5. 10p Add profiling code. Measure the number of VMEXITs triggered by the VMM.

Submitting the assigment
------------------------

The assignment archive will be submitted on **Moodle**, according to the rules on the `rules page <https://ocw.cs.pub.ro/courses/so2/reguli-notare#reguli_de_trimitere_a_temelor>`__.


Tips
----

To increase your chances of getting the highest grade, read and follow the Linux kernel coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

Also, use the following static analysis tools to verify the code:

* checkpatch.pl

  .. code-block:: console

     $ linux/scripts/checkpatch.pl --no-tree --terse -f /path/to/your/file.c

* sparse

  .. code-block:: console

     $ sudo apt-get install sparse
     $ cd linux
     $ make C=2 /path/to/your/file.c

* cppcheck

  .. code-block:: console

     $ sudo apt-get install cppcheck
     $ cppcheck /path/to/your/file.c

Penalties
---------

Information about assigments penalties can be found on the `General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__.

In exceptional cases (the assigment passes the tests by not complying with the requirements) and if the assigment does not pass all the tests, the grade will may decrease more than mentioned above.

## References
We recommend you the following readings before starting to work on the homework:
* [KVM host in a few lines of code](https://zserge.com/posts/kvm/)

  
TLDR
----

1. The VMM creates and initializes a virtual machine and a virtual CPU
2. We switch to real mode and check run the simple guest code from `guest_16_bits`
3. We switch to long mode and run the more complex guest from `guest_32_bits`
4. We implement the SIMVIRTIO protocol. We will describe how it behaves in the following subtasks.
5. The guest writes in the TX queue (queue 0) the ascii code for `R` which will result in a `VMEXIT`
6. the VMM will handle the VMEXIT caused by the previous write in the queue. When the guests receiver the
`R` letter it will initiate the reser procedure of the device and set the device status to `DEVICE_RESET`
7. After the reset handling, the guest must set the status of the device to `DRIVER_ACK`. After this, the guest will write to the TX queue the letter `C`
8. In the VMM we will initialize the config process when letter `C` is received.It will set the device status to `DEVICE_CONFIG` and add a new entry in the device_table
9. After the configuration process is finished, the guest will set the driver status to `DRIVER_OK`
10. Nex, the VMM will set the device status to `DEVICE_READY`
11. The guest will write in the TX queue "Ana are mere" and will execute a halt
12. The VMM will print to the STDOUT the message received and execute the halt request
13. Finally, the VMM will verify that at address 0x400 and in register RAX is stored the value 42


