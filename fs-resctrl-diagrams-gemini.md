# Mermaid Diagrams for `resctrl` Function Flows

This document outlines the important function flows in `fs/resctrl/rdtgroup.c` for creating the `resctrl` filesystem directory structure.

## 1. Filesystem Initialization and Mount Flow

This flow describes the two main stages of `resctrl` setup:
1.  **Module Initialization**: Key data structures are initialized and the `resctrl` filesystem type is registered with the kernel. This happens when the `resctrl` kernel module is loaded.
2.  **Mount-time Creation**: When a user mounts the filesystem (e.g., `mount -t resctrl resctrl /sys/fs/resctrl`), the directory and file hierarchy is constructed in memory via `kernfs`.

```mermaid
graph TD
    subgraph Kernel Module Init
        A(resctrl_init) -- in fs/resctrl/core.c --> B(rdt_init_resctrl_fs);
        B -- in fs/resctrl/core.c --> C(rdtgroup_init);
        C -- in fs/resctrl/rdtgroup.c --> D["register_filesystem(&resctrl_fs_type)"];
    end

    subgraph User Mounts Filesystem
        E(user: mount -t resctrl ...) --> F(resctrl_mount);
    end

    subgraph Mount-time Directory Creation
        F -- in fs/resctrl/rdtgroup.c --> G(rdtgroup_mount);
        G --> H["kern_mount(&resctrl_fs_type)"];
        G --> I(rdtgroup_info_dir_create);
        I --> J["kernfs_create_dir('info')"];
        I --> K(rdtgroup_info_populate);
        K --> L(rdt_info_files_create);
        L --> M["kernfs_create_file(...) for each info file"];

        G --> N(__rdtgroup_create_info_dir);
        N -- for root group --> O(rdtgroup_add_files);
        O -- creates 'schemata' file --> P["kernfs_create_file('schemata')"];
        G --> Q["Create 'tasks', 'cpus', 'cpus_list'<br/>via kernfs_create_file"];
    end

    D -.-> E;
```

## 2. New Resource Group Creation (`mkdir`) Flow

The `resctrl` filesystem allows users to create new resource groups by creating new directories. The `mkdir` operation triggers the following kernel flow to create a new `rdtgroup` and its associated control files (`schemata`, `tasks`, etc.).

```mermaid
graph TD
    subgraph User Action
        A(user: mkdir /sys/fs/resctrl/my_group) --> B(resctrl_mkdir);
    end

    subgraph mkdir Implementation in rdtgroup.c
        B -- in fs/resctrl/rdtgroup.c --> C(rdtgroup_mkdir);
        C --> D["Allocate new rdtgroup struct"];
        C --> E(rdtgroup_kn_alloc);
        E --> F["Allocates kernfs_node for 'my_group'"];
        C --> G(__rdtgroup_create);
        
        subgraph __rdtgroup_create [in __rdtgroup_create]
            direction LR
            G --> H(__rdtgroup_create_info_dir);
            H --> I["rdtgroup_add_files(..., rdt_info_files, ...)"];
            I --> J["kernfs_create_file('schemata')"];
            G --> K["rdtgroup_add_files(..., rdt_base_files, ...)"];
            K --> L["kernfs_create_file('tasks')<br/>kernfs_create_file('cpus')<br/>..."];
        end

        C --> O(kernfs_activate);
        O --> P["New directory 'my_group' becomes visible"];
    end
```

These diagrams illustrate the core logic within `fs/resctrl` for managing the filesystem structure.