.. SPDX-License-Identifier: GPL-2.0

=======================================================
Dicas de depuração para desenvolvedores do Kernel Linux
=======================================================

Guias gerais
------------

Todolist:

*   driver_development_debugging_guide
*   gdb-kernel-debugging
*   kgdb
*   userspace_debugging_guide

Guias específicos de subsistemas
--------------------------------

Todolist:

*   media_specific_debugging_guide

Dicas gerais de depuração
-------------------------

Dependendo do problema, um conjunto diferente de ferramentas está disponível
para rastrear o problema ou até mesmo para perceber se há algum problema em
primeiro lugar.

Como primeiro passo, você precisa descobrir que tipo de problema você deseja
depurar. Dependendo da resposta, sua metodologia e escolha de ferramentas podem
variar.

Preciso depurar com acesso limitado?
------------------------------------

Você possui acesso limitado à máquina ou não consegue parar a execução em
andamento?

Nesse caso, sua capacidade de depuração depende do suporte de depuração
embutido no kernel fornecido pela distribuição.
O :doc:`/process/debugging/userspace_debugging_guide` fornece uma breve visão
geral sobre uma variedade de ferramentas de depuração possíveis nessa situação.
Você pode verificar a capacidade do seu kernel, na maioria dos casos, olhando o
arquivo de configuração dentro do diretório /boot.

Eu tenho acesso root ao sistema?
--------------------------------

Você consegue facilmente substituir o módulo em questão ou instalar um novo
kernel?

Nesse caso, sua gama de ferramentas disponíveis é muito maior. Você
pode encontrar as ferramentas
no :doc:`/process/debugging/driver_development_debugging_guide`.

A temporização é um fator?
--------------------------

É importante entender se o problema que você deseja depurar se manifesta
de forma consistente (ou seja, para um determinado conjunto de entradas, você
sempre obtém a mesma saída incorreta) ou de forma inconsistente. Se ele se
manifestar de forma inconsistente, algum fator de temporização pode estar em
jogo. Se a inserção de atrasos no código alterar o comportamento, é bastante
provável que a temporização seja um fator determinante.

Quando a temporização altera o resultado da execução do código, o uso de um
simples printk() para fins de depuração pode não funcionar; uma alternativa
semelhante é usar trace_printk(), que registra as mensagens de depuração no
arquivo de rastreamento, em vez de no log do kernel.

**Copyright** ©2024 : Collabora
